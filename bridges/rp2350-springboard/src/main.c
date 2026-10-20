#include "bus_model.h"
#include "capture_program.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "rom_image.h"

#if SB_USB_DEBUG
#include "pico/stdio_usb.h"
#include <stdarg.h>
#include <stdio.h>
#endif

_Static_assert(PICO_DEFAULT_LED_PIN == 39, "LED must stay outside bus GPIOs");

/* SWD symbols. Stop capture via sb_debug_stop, then wait for sb_debug_stopped
 * before reading records. No stdio/UART uses Springboard GPIOs. */
volatile bool sb_debug_stop, sb_debug_arm, sb_debug_stopped = true;
volatile bool sb_debug_capture_ready, sb_debug_limit;
volatile uint32_t sb_debug_run, sb_debug_queued[2];
volatile bool sb_debug_usb_ready;
volatile uint32_t sb_debug_rom_ready, sb_debug_fifo_stalls[2];
volatile uint32_t sb_debug_gpio_low, sb_debug_gpio_high;
struct sb_trace sb_debug_trace;
struct sb_reset_monitor sb_debug_reset;
uint8_t sb_rom_sram[SB_ROM_CAPACITY];

#if SB_USB_DEBUG
/* Best effort: do not call printf/puts/stdio_flush, which may flush/wait.
 * Both SDK output-space and mutex wait timeouts are zero for this target. */
static void debug_emit(const char *format, ...) {
    char line[192];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (length > 0)
        stdio_put_string(
            line, length < (int)sizeof(line) ? length : (int)sizeof(line) - 1,
            false, false);
}

static void debug_status(void) {
    debug_emit(
        "rom_ready=%u capture_ready=%u stopped=%u "
        "fifo_stalls=%lx/%lx gpio=%08lx/%08lx\r\n",
        (unsigned)sb_debug_rom_ready, (unsigned)sb_debug_capture_ready,
        (unsigned)sb_debug_stopped, (unsigned long)sb_debug_fifo_stalls[0],
        (unsigned long)sb_debug_fifo_stalls[1],
        (unsigned long)sb_debug_gpio_low, (unsigned long)sb_debug_gpio_high);
}

static void debug_poll(uint64_t now) {
    static uint64_t poll_at, announce_at, status_at;
    static bool connected, dumping, status_detail;
    static unsigned dump_next;
    if (!sb_debug_usb_ready || now < poll_at)
        return;
    poll_at = now + 10000;
    if (!stdio_usb_connected()) {
        connected = false;
        dumping = false;
        status_detail = false;
        return;
    }
    if (!connected) {
        connected = true;
        announce_at = now + 250000;
        return;
    }
    if (announce_at) {
        if (now < announce_at)
            return;
        announce_at = 0;
        debug_emit("FujiSpring passive / RP2350B / USB debug v3\r\n"
                   "clock=%lu Hz ROM=%u bytes CRC32=%08lx\r\n"
                   "h=help a=arm s=status r=RESET x=stop d=dump\r\n",
                   (unsigned long)clock_get_hz(clk_sys),
                   (unsigned)SB_ROM_IMAGE_SIZE,
                   (unsigned long)SB_ROM_IMAGE_CRC);
        status_at = now;
        return; /* Give the USB host a pass before the first status line. */
    }
    if (status_detail) {
        debug_emit("run=%lu arm_pending=%u limit=%u queued=%lu/%lu\r\n",
                   (unsigned long)sb_debug_run, (unsigned)sb_debug_arm,
                   (unsigned)sb_debug_limit, (unsigned long)sb_debug_queued[0],
                   (unsigned long)sb_debug_queued[1]);
        status_detail = false;
        return;
    }
    /* At most one input byte and one trace record per 10 ms pass. */
    const int command = getchar_timeout_us(0);
    switch (command) {
    case 'h':
        debug_emit(
            "a=clear/arm s=status r=RESET x=stop d=dump; starts stopped\r\n");
        break;
    case 'a':
        if (!sb_debug_capture_ready || !sb_debug_stopped || sb_debug_arm) {
            debug_emit("arm refused: wait for stopped=1 arm_pending=0\r\n");
        } else {
            dumping = false;
            __dmb();
            sb_debug_arm = true;
            debug_emit("arm requested; s confirms new run and stopped=0\r\n");
            status_at = now + 100000;
        }
        break;
    case 's':
        status_at = now;
        break;
    case 'r':
        debug_emit("reset_level=%u asserted=%lu released=%lu initialized=%u\r\n",
                   (unsigned)sb_debug_reset.level,
                   (unsigned long)sb_debug_reset.asserted,
                   (unsigned long)sb_debug_reset.released,
                   (unsigned)sb_debug_reset.initialized);
        break;
    case 'x':
        if (sb_debug_arm) {
            debug_emit("stop refused: arm pending; wait for arm_pending=0\r\n");
            break;
        }
        sb_debug_stop = true;
        debug_emit("capture stop requested; wait for stopped=1 before d\r\n");
        break;
    case 'd':
        if (!sb_debug_stopped || sb_debug_arm) {
            debug_emit("trace is live; x stops capture, then d dumps it\r\n");
        } else {
            __dmb();
            dump_next = 0;
            dumping = true;
            debug_emit("trace run=%lu count=%lu dropped=%lu limit=%u; unpaired\r\n",
                       (unsigned long)sb_debug_run,
                       (unsigned long)sb_debug_trace.count,
                       (unsigned long)sb_debug_trace.dropped,
                       (unsigned)sb_debug_limit);
            return; /* Header and first record use separate USB passes. */
        }
        break;
    default:
        break;
    }
    if (now >= status_at) {
        debug_status();
        status_detail = true;
        status_at = now + 5000000;
        return;
    }
    if (dumping) {
        if (dump_next < sb_debug_trace.count &&
            dump_next < SB_CAPTURE_CAPACITY) {
            const struct sb_capture record = sb_debug_trace.records[dump_next];
            debug_emit("%u window=%u cs=%u sequence=%lu sample=%08lx\r\n",
                       dump_next++, (unsigned)record.window,
                       (unsigned)record.cs, (unsigned long)record.sequence,
                       (unsigned long)record.sample);
        } else {
            dumping = false;
            debug_emit("trace end\r\n");
        }
    }
}
#endif

/* Only core 1 touches PIO, trace storage and per-run counters. Core 0 requests
 * arm/stop and reads the trace only after the stopped acknowledgement + barrier.
 */
static void capture_disable(void) {
    pio_set_sm_mask_enabled(pio0, 3, false);
    pio_set_sm_mask_enabled(pio1, 3, false);
    for (unsigned block = 0; block < 2; ++block) {
        PIO pio = block ? pio1 : pio0;
        sb_debug_fifo_stalls[block] |= pio->fdebug & 0xfu;
        sb_debug_queued[block] = pio_sm_get_rx_fifo_level(pio, 0) +
                                 pio_sm_get_rx_fifo_level(pio, 1);
    }
}

static void capture_prepare(void) {
    for (unsigned block = 0; block < 2; ++block) {
        PIO pio = block ? pio1 : pio0;
        pio_set_sm_mask_enabled(pio, 3, false);
        for (unsigned sm = 0; sm < 2; ++sm) {
            pio_sm_clear_fifos(pio, sm);
            pio_sm_restart(pio, sm);
        }
        pio->fdebug = 0xffffffffu; /* W1C, discard previous run's flags. */
        sb_debug_fifo_stalls[block] = 0;
        sb_debug_queued[block] = 0;
    }
    sb_trace_clear(&sb_debug_trace);
    sb_debug_limit = false;
}

static void __not_in_flash_func(capture_core)(void) {
    /* Configure once to establish readiness, but never capture before arming. */
    sb_capture_program_init(false);
    capture_disable();
    __dmb();
    sb_debug_capture_ready = true;
    for (;;) {
        __dmb();
        sb_debug_stopped = true;
        while (!sb_debug_arm)
            tight_loop_contents();
        __dmb();
        uint32_t sequence[2][2] = {{0}};
        capture_prepare();
        sb_debug_stop = false;
        ++sb_debug_run;
        sb_debug_stopped = false;
        sb_capture_program_init(true); /* PC, FIFOs and flags already reset. */
        __dmb();
        sb_debug_arm = false;
        while (!sb_debug_stop && !sb_trace_full(&sb_debug_trace)) {
            for (unsigned block = 0; block < 2; ++block) {
                PIO pio = block ? pio1 : pio0;
                sb_debug_fifo_stalls[block] |= pio->fdebug & 0xfu;
                for (unsigned sm = 0; sm < 2; ++sm) {
                    if (sb_debug_stop || sb_trace_full(&sb_debug_trace))
                        break;
                    if (!pio_sm_is_rx_fifo_empty(pio, sm)) {
                        struct sb_capture record = {
                            .sample = pio_sm_get(pio, sm),
                            .sequence = sequence[block][sm]++,
                            .window = (uint8_t)(block * 16),
                            .cs = (uint8_t)sm};
                        sb_trace_push(&sb_debug_trace, record);
                    }
                }
            }
        }
        sb_debug_limit = sb_trace_full(&sb_debug_trace);
        capture_disable();
    }
}

int main(void) {
    /* Disable all bus output enables before clock changes or copying the ROM.
     * GP39 is initialized separately for the LED; leave GP47 (PSRAM CS) alone.
     */
    const uint64_t mask = ((UINT64_C(1) << 47) - 1) & ~(UINT64_C(1) << 39);
    gpio_init_mask64(mask);
    gpio_set_dir_in_masked64(mask);
    for (unsigned pin = 0; pin <= 46; ++pin)
        if (pin != 39)
            gpio_disable_pulls(pin);
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_put(PICO_DEFAULT_LED_PIN, true);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    set_sys_clock_khz(150000, true);
    sb_debug_rom_ready =
        sb_load_rom(sb_rom_sram, sizeof(sb_rom_sram), sb_rom_image,
                    SB_ROM_IMAGE_SIZE, SB_ROM_IMAGE_CRC);
    multicore_launch_core1(capture_core);
#if SB_USB_DEBUG
    /* Initialize on core 0 after launching capture; never wait for a host. */
    sb_debug_usb_ready = stdio_usb_init();
#endif
    bool led_on = true;
    uint64_t led_check_at = 0;
    for (;;) {
        /* These two live samples are intentionally not claimed atomic. */
        sb_debug_gpio_low = sio_hw->gpio_in;
        sb_debug_gpio_high = sio_hw->gpio_hi_in;
        sb_monitor_reset(&sb_debug_reset,
                         (sb_debug_gpio_high & (1u << (45 - 32))) != 0);
        /* Core 0 only: no LED work or delays in the capture core. */
        const uint64_t now = time_us_64();
#if SB_USB_DEBUG
        debug_poll(now);
#endif
        if (now >= led_check_at) {
            bool on;
            if (!sb_debug_rom_ready)
                on = now % 200000 < 100000;
            else if (!sb_debug_capture_ready || sb_debug_stopped)
                on = true;
            else
                on = now % 1000000 < 100000;
            if (on != led_on) {
                gpio_put(PICO_DEFAULT_LED_PIN, on);
                led_on = on;
            }
            led_check_at = now + 10000;
        }
        tight_loop_contents();
    }
}
