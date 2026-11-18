#include "fujinet/diag/diagnostic_usb_host.h"
#include <utility>

namespace fujinet::diag {
namespace {
class UsbHostProvider final : public IDiagnosticProvider {
public:
    explicit UsbHostProvider(std::function<UsbHostSnapshot()> snapshot) : _snapshot(std::move(snapshot)) {}
    std::string_view provider_id() const noexcept override { return "usb"; }
    void list_commands(std::vector<DiagCommandSpec>& out) const override {
        out.push_back({"usb.status", "show CDC host connection and counters", "usb.status", true});
    }
    DiagResult execute(const DiagArgsView& args) override {
        if (args.argv.empty()) return DiagResult::invalid_args("missing command");
        if (args.argv[0] != "usb.status") return DiagResult::not_found("unknown USB command");
        if (args.argv.size() != 1) return DiagResult::invalid_args("usage: usb.status");
        const auto s = _snapshot();
        std::string text = "state: " + s.state + "\r\n";
        text += "function: 2e8a:0009 NIO CDC 0/1 (console/reset untouched)\r\n";
        const auto line = [&](const char* key, auto value) { text += std::string(key) + ": " + std::to_string(value) + "\r\n"; };
        line("bulk_in", s.bulkIn); line("bulk_out", s.bulkOut);
        line("epoch", s.epoch); line("buffered_rx", s.buffered);
        line("connections", s.stats.connections);
        line("reconnects", s.stats.connections ? s.stats.connections - 1 : 0);
        line("disconnects", s.stats.disconnects); line("errors", s.stats.errors);
        line("rx_overflows", s.stats.overflows); line("tx_failures", s.stats.txFailures);
        line("rx_bytes", s.stats.rxBytes); line("tx_bytes", s.stats.txBytes);
        line("last_error", s.lastError);
        return DiagResult::ok(std::move(text));
    }
private:
    std::function<UsbHostSnapshot()> _snapshot;
};
}
std::unique_ptr<IDiagnosticProvider> create_usb_host_diagnostic_provider(std::function<UsbHostSnapshot()> snapshot) {
    return std::make_unique<UsbHostProvider>(std::move(snapshot));
}
} // namespace fujinet::diag
