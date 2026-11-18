#pragma once

#include "fujinet/diag/diagnostic_provider.h"
#include "fujinet/io/usb_cdc_host_session.h"
#include <functional>
#include <string>

namespace fujinet::diag {
struct UsbHostSnapshot {
    std::string state;
    io::UsbCdcHostSession::Stats stats;
    std::uint32_t epoch{};
    std::size_t buffered{};
    std::uint8_t bulkIn{}, bulkOut{};
    int lastError{};
};
std::unique_ptr<IDiagnosticProvider> create_usb_host_diagnostic_provider(std::function<UsbHostSnapshot()> snapshot);
} // namespace fujinet::diag
