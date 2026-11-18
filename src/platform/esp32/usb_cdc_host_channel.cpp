#include "sdkconfig.h"

#if defined(FN_BUILD_ESP32_USB_HOST_LINK)

#include "fujinet/platform/esp32/usb_cdc_host_channel.h"
#include "fujinet/io/usb_cdc_host_session.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"
#include <atomic>
#include <mutex>

#if CONFIG_TINYUSB_CDC_ENABLED || CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG || CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG
#error "CDC host requires exclusive native USB PHY ownership; use UART console/logs"
#endif

namespace fujinet::platform::esp32 {
namespace {
constexpr const char* TAG = "usb_host";
constexpr std::uint16_t kVid = 0x2e8a, kPid = 0x0009;
using Session = io::UsbCdcHostSession;
}

struct UsbCdcHostChannel::Impl {
    mutable std::mutex mutex;
    Session session;
    const std::size_t recordSize;
    io::SpringboardCdcFunction function{};
    bool candidate{};
    std::uint32_t candidateGeneration{};
    int lastError{};
    std::atomic<bool> stopping{false}, daemonStopping{false}, allFree{false};
    SemaphoreHandle_t readable{xSemaphoreCreateBinary()};
    SemaphoreHandle_t workerDone{xSemaphoreCreateBinary()};
    SemaphoreHandle_t daemonDone{xSemaphoreCreateBinary()};
    TaskHandle_t worker{};
    inline static std::atomic<Impl*> active{nullptr};

    explicit Impl(std::size_t capacity) : session(capacity + 10), recordSize(capacity + 10) {
        // Exactly one root-port bridge; the runtime owns the host stack.
        Impl* expected = nullptr;
        if (!readable || !workerDone || !daemonDone || !active.compare_exchange_strong(expected, this)) {
            std::lock_guard<std::mutex> lock(mutex);
            lastError = ESP_ERR_NO_MEM;
            session.stop();
            return;
        }
        if (xTaskCreate(worker_entry, "nio_usb_host", 6144, this, 5, &worker) != pdPASS) {
            active = nullptr;
            lastError = ESP_ERR_NO_MEM;
            session.stop();
        }
    }
    ~Impl() {
        stopping = true;
        if (worker) {
            // Driver control operations have a 5 s bound; only teardown waits here,
            // never the cooperative core's normal path.
            xSemaphoreTake(workerDone, portMAX_DELAY);
        }
        if (active == this) active = nullptr;
        if (readable) vSemaphoreDelete(readable);
        if (workerDone) vSemaphoreDelete(workerDone);
        if (daemonDone) vSemaphoreDelete(daemonDone);
    }
    static std::uint32_t callback_epoch(void* arg) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(arg)); }
    static bool rx(const std::uint8_t* bytes, std::size_t size, void* arg) {
        auto* self = active.load();
        if (!self) return true;
        {
            std::lock_guard<std::mutex> lock(self->mutex);
            const auto epoch = callback_epoch(arg);
            const auto before = self->session.epoch();
            self->session.append(epoch, bytes, size);
            if (before != self->session.epoch()) self->lastError = ESP_ERR_NO_MEM;
        }
        xSemaphoreGive(self->readable);
        return true; // We copied or explicitly discarded this buffer.
    }
    static void event(const cdc_acm_host_dev_event_data_t* event, void* arg) {
        if (event->type != CDC_ACM_HOST_DEVICE_DISCONNECTED && event->type != CDC_ACM_HOST_ERROR) return;
        auto* self = active.load();
        if (!self) return;
        {
            std::lock_guard<std::mutex> lock(self->mutex);
            const auto epoch = callback_epoch(arg);
            if (epoch != self->session.epoch()) return;
            self->lastError = event->type == CDC_ACM_HOST_ERROR ? event->data.error : ESP_ERR_NOT_FOUND;
            if (event->type == CDC_ACM_HOST_DEVICE_DISCONNECTED) self->candidate = false;
            self->session.fail(epoch, event->type == CDC_ACM_HOST_DEVICE_DISCONNECTED);
        }
        xSemaphoreGive(self->readable);
    }
    static void new_device(usb_device_handle_t device) {
        auto* self = active.load();
        if (!self) return;
        const usb_device_desc_t* desc = nullptr;
        const usb_config_desc_t* cfg = nullptr;
        io::SpringboardCdcFunction function;
        const bool valid = usb_host_get_device_descriptor(device, &desc) == ESP_OK &&
            desc->idVendor == kVid && desc->idProduct == kPid &&
            usb_host_get_active_config_descriptor(device, &cfg) == ESP_OK &&
            io::springboard_nio_cdc(reinterpret_cast<const std::uint8_t*>(cfg), cfg->wTotalLength, function);
        {
            std::lock_guard<std::mutex> lock(self->mutex);
            self->candidate = valid;
            ++self->candidateGeneration;
            if (valid) self->function = function;
            else { self->lastError = ESP_ERR_NOT_SUPPORTED; }
        }
        // The class driver's task owns this callback. Never open here.
    }
    static void daemon_entry(void* arg) {
        auto& self = *static_cast<Impl*>(arg);
        while (!self.daemonStopping) {
            std::uint32_t flags{};
            const auto err = usb_host_lib_handle_events(pdMS_TO_TICKS(100), &flags);
            if (err != ESP_OK && err != ESP_ERR_TIMEOUT) ESP_LOGE(TAG, "host events: %s", esp_err_to_name(err));
            if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
            if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) self.allFree = true;
        }
        xSemaphoreGive(self.daemonDone);
        vTaskDelete(nullptr);
    }
    static void worker_entry(void* arg) {
        static_cast<Impl*>(arg)->run();
    }
    void run() {
        usb_host_config_t host{};
        host.intr_flags = ESP_INTR_FLAG_LEVEL1;
        auto err = usb_host_install(&host);
        bool installed = err == ESP_OK, daemon = false, driver = false;
        if (installed) {
            daemon = xTaskCreate(daemon_entry, "nio_usb_events", 4096, this, 6, nullptr) == pdPASS;
            if (!daemon) err = ESP_ERR_NO_MEM;
        }
        if (daemon) {
            cdc_acm_host_driver_config_t cfg{};
            cfg.driver_task_stack_size = 4096;
            cfg.driver_task_priority = 6;
            cfg.xCoreID = 0;
            cfg.new_dev_cb = new_device;
            err = cdc_acm_host_install(&cfg);
            driver = err == ESP_OK;
        }
        if (driver) ESP_LOGI(TAG, "CDC host ready; waiting for Springboard NIO 2e8a:0009 CDC 0/1");
        else {
            ESP_LOGE(TAG, "host startup: %s", esp_err_to_name(err));
            std::lock_guard<std::mutex> lock(mutex);
            lastError = err;
            session.fail(session.epoch());
        }
        cdc_acm_dev_hdl_t handle = nullptr;
        std::uint32_t epoch = 0;
        while (driver && !stopping) {
            bool recover = false, open = false;
            std::uint32_t candidateAtOpen{};
            std::vector<std::uint8_t> tx;
            {
                std::lock_guard<std::mutex> lock(mutex);
                recover = handle && session.state() == Session::State::Recovering;
                if (!handle && candidate) {
                    epoch = session.begin_open();
                    candidateAtOpen = candidateGeneration;
                    open = true;
                }
                if (handle && session.state() == Session::State::Connected) tx = session.take_tx();
            }
            if (recover) {
                // Invalidate callbacks before any driver operation. Handles are
                // owned only here; close never races transmit or open.
                ESP_LOGW(TAG, "recovering CDC session");
                // Deassert DTR so reopening this physical device starts a fresh
                // bridge session after overflow/TX failure, too.
                cdc_acm_host_set_control_line_state(handle, false, false);
                cdc_acm_host_close(handle);
                handle = nullptr;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    session.waiting();
                }
            } else if (open) {
                cdc_acm_host_device_config_t cfg{};
                cfg.connection_timeout_ms = 250;
                cfg.out_buffer_size = recordSize;
                cfg.in_buffer_size = 512;
                cfg.event_cb = event;
                cfg.data_cb = rx;
                // Immutable numeric token, no allocated callback context to
                // free/reuse. Late callbacks are rejected against session.epoch.
                cfg.user_arg = reinterpret_cast<void*>(static_cast<std::uintptr_t>(epoch));
                err = cdc_acm_host_open(kVid, kPid, 0, &cfg, &handle);
                if (err == ESP_OK) {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (!candidate || candidateAtOpen != candidateGeneration || session.epoch() != epoch)
                        err = ESP_ERR_INVALID_STATE;
                }
                if (err == ESP_OK) {
                    cdc_acm_line_coding_t line{};
                    line.dwDTERate = 115200;
                    line.bDataBits = 8;
                    err = cdc_acm_host_line_coding_set(handle, &line);
                    if (err == ESP_OK) err = cdc_acm_host_set_control_line_state(handle, true, false);
                }
                bool connected = false;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (err == ESP_OK && candidateAtOpen == candidateGeneration && candidate)
                        connected = session.connected(epoch);
                    if (!connected) { lastError = err == ESP_OK ? ESP_ERR_INVALID_STATE : err; session.fail(epoch); }
                }
                if (connected) {
                    ESP_LOGI(TAG, "NIO CDC 0/1 open, DTR asserted (epoch %lu)", static_cast<unsigned long>(epoch));
                    xSemaphoreGive(readable);
                } else {
                    if (handle) {
                        cdc_acm_host_set_control_line_state(handle, false, false);
                        cdc_acm_host_close(handle);
                        handle = nullptr;
                    }
                    std::lock_guard<std::mutex> lock(mutex);
                    session.waiting();
                }
            } else if (!tx.empty()) {
                err = cdc_acm_host_data_tx_blocking(handle, tx.data(), tx.size(), 250);
                std::lock_guard<std::mutex> lock(mutex);
                if (err != ESP_OK) lastError = err;
                session.tx_complete(epoch, tx.size(), err == ESP_OK);
            }
            // Polling is bounded and independent of the cooperative core. No
            // task handle is exposed to callbacks or channel operations.
            vTaskDelay(pdMS_TO_TICKS(open && !handle ? 250 : 10));
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            session.stop();
        }
        if (handle) cdc_acm_host_close(handle);
        if (driver) cdc_acm_host_uninstall();
        if (daemon) {
            if (usb_host_device_free_all() == ESP_OK) allFree = true;
            for (int i = 0; i < 100 && !allFree; ++i) vTaskDelay(pdMS_TO_TICKS(10));
            daemonStopping = true;
            usb_host_lib_unblock();
            xSemaphoreTake(daemonDone, portMAX_DELAY);
        }
        if (installed) usb_host_uninstall();
        xSemaphoreGive(workerDone);
        vTaskDelete(nullptr);
    }
};

UsbCdcHostChannel::UsbCdcHostChannel(std::size_t capacity) : _impl(std::make_unique<Impl>(capacity)) {}
UsbCdcHostChannel::~UsbCdcHostChannel() = default;
bool UsbCdcHostChannel::available() {
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->session.available();
}
std::size_t UsbCdcHostChannel::read(std::uint8_t* bytes, std::size_t size) {
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->session.read(bytes, size);
}
void UsbCdcHostChannel::write(const std::uint8_t* bytes, std::size_t size) {
    write_for_epoch(bytes, size, connection_epoch());
}
bool UsbCdcHostChannel::write_for_epoch(const std::uint8_t* bytes, std::size_t size, std::uint32_t epoch) {
    bool accepted;
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        accepted = _impl->session.enqueue(epoch, bytes, size);
    }
    return accepted;
}
std::uint32_t UsbCdcHostChannel::connection_epoch() const {
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->session.epoch();
}
bool UsbCdcHostChannel::wait_for_readable(std::chrono::milliseconds timeout) {
    if (available()) return true;
    if (_impl->readable) xSemaphoreTake(_impl->readable, pdMS_TO_TICKS(timeout.count()));
    return available();
}
diag::UsbHostSnapshot UsbCdcHostChannel::snapshot() const {
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return {_impl->session.state_name(), _impl->session.stats(), _impl->session.epoch(),
            _impl->session.buffered(), _impl->function.bulkIn, _impl->function.bulkOut, _impl->lastError};
}
} // namespace fujinet::platform::esp32
#endif
