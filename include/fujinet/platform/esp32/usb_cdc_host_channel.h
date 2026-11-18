#pragma once

#include "fujinet/io/core/channel.h"
#include "fujinet/diag/diagnostic_usb_host.h"
#include <memory>

namespace fujinet::platform::esp32 {
class UsbCdcHostChannel final : public io::Channel {
public:
    explicit UsbCdcHostChannel(std::size_t packetCapacity);
    ~UsbCdcHostChannel() override;
    bool available() override;
    std::size_t read(std::uint8_t*, std::size_t) override;
    void write(const std::uint8_t*, std::size_t) override;
    bool write_for_epoch(const std::uint8_t*, std::size_t, std::uint32_t) override;
    std::uint32_t connection_epoch() const override;
    bool supports_readable_wait() const override { return true; }
    bool wait_for_readable(std::chrono::milliseconds) override;
    diag::UsbHostSnapshot snapshot() const;
private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
} // namespace fujinet::platform::esp32
