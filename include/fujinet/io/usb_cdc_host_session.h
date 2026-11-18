#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fujinet::io {

// Portable state owned and locked by the platform host runtime. Each callback
// carries the epoch from opening; late callbacks cannot affect another session.
class UsbCdcHostSession {
public:
    enum class State { Waiting, Opening, Connected, Recovering, Stopped };
    struct Stats {
        std::uint32_t connections{}, disconnects{}, errors{}, overflows{}, txFailures{};
        std::uint64_t rxBytes{}, txBytes{};
    };
    explicit UsbCdcHostSession(std::size_t recordSize)
        : _rx(2 * recordSize), _recordSize(recordSize) { _tx.reserve(recordSize); }

    std::uint32_t begin_open() { clear(); ++_epoch; _state = State::Opening; return _epoch; }
    bool connected(std::uint32_t epoch) {
        if (epoch != _epoch || _state != State::Opening) return false;
        _state = State::Connected;
        ++_stats.connections;
        return true;
    }
    void fail(std::uint32_t epoch, bool disconnect = false) {
        if (epoch != _epoch) return;
        if (disconnect) ++_stats.disconnects; else ++_stats.errors;
        ++_epoch;
        clear();
        _state = State::Recovering;
    }
    void waiting() { clear(); _state = State::Waiting; }
    void stop() { ++_epoch; clear(); _state = State::Stopped; }
    bool append(std::uint32_t epoch, const std::uint8_t* bytes, std::size_t size) {
        if (!accepts(epoch)) return false;
        if (size > _rx.size() - _size) {
            ++_stats.overflows;
            fail(epoch);
            return false;
        }
        for (std::size_t i = 0; i < size; ++i) _rx[(_head + _size + i) % _rx.size()] = bytes[i];
        _size += size;
        _stats.rxBytes += size;
        return true;
    }
    std::size_t read(std::uint8_t* bytes, std::size_t limit) {
        if (_state != State::Connected) return 0;
        const auto size = std::min(limit, _size);
        for (std::size_t i = 0; i < size; ++i) bytes[i] = _rx[(_head + i) % _rx.size()];
        _head = (_head + size) % _rx.size();
        _size -= size;
        return size;
    }
    bool enqueue(std::uint32_t epoch, const std::uint8_t* bytes, std::size_t size) {
        if (epoch != _epoch || _state != State::Connected) return false;
        if (!size || size > _recordSize || !_tx.empty()) {
            ++_stats.txFailures;
            fail(epoch);
            return false;
        }
        _tx.assign(bytes, bytes + size);
        return true;
    }
    std::vector<std::uint8_t> take_tx() {
        std::vector<std::uint8_t> out;
        out.swap(_tx);
        return out;
    }
    void tx_complete(std::uint32_t epoch, std::size_t size, bool success) {
        if (epoch != _epoch) return;
        if (success) _stats.txBytes += size;
        else { ++_stats.txFailures; fail(epoch); }
    }
    bool available() const { return _state == State::Connected && _size != 0; }
    std::uint32_t epoch() const { return _epoch; }
    State state() const { return _state; }
    const Stats& stats() const { return _stats; }
    std::size_t buffered() const { return _size; }
    const char* state_name() const {
        switch (_state) {
        case State::Waiting: return "waiting";
        case State::Opening: return "opening";
        case State::Connected: return "connected";
        case State::Recovering: return "recovering";
        default: return "stopped";
        }
    }
private:
    bool accepts(std::uint32_t epoch) const {
        return epoch == _epoch && (_state == State::Opening || _state == State::Connected);
    }
    void clear() { _head = _size = 0; _tx.clear(); }
    std::vector<std::uint8_t> _rx, _tx;
    std::size_t _recordSize, _head{}, _size{};
    std::uint32_t _epoch{};
    State _state{State::Waiting};
    Stats _stats;
};

struct SpringboardCdcFunction { std::uint8_t bulkIn{}, bulkOut{}; };

// Identify only the existing Springboard NIO function (IAD + ACM 0/1 +
// union descriptor). Endpoint addresses are discovered, not assumed. The
// console at 2/3 and the vendor reset interface are never selected.
inline bool springboard_nio_cdc(const std::uint8_t* cfg, std::size_t size, SpringboardCdcFunction& out)
{
    out = {};
    if (!cfg || size < 9 || cfg[0] != 9 || cfg[1] != 2) return false;
    const std::size_t total = cfg[2] | (std::size_t(cfg[3]) << 8);
    if (total != size || cfg[5] != 1) return false;
    bool iad = false, comm = false, data = false, unionDesc = false, acm = false;
    int interface = -1;
    unsigned in = 0, outCount = 0;
    for (std::size_t at = 9; at < total;) {
        const auto* d = cfg + at;
        const auto n = d[0];
        if (n < 2 || n > total - at) return false;
        if (d[1] == 11 && n >= 8 && d[2] == 0) {
            if (iad) return false;
            iad = d[3] == 2 && d[4] == 2 && d[5] == 2 && d[6] == 0;
        } else if (d[1] == 4) {
            if (n < 9) return false;
            interface = d[2];
            if (interface == 0) {
                if (comm || d[3] || d[4] != 1 || d[5] != 2 || d[6] != 2 || d[7] != 0) return false;
                comm = true;
            }
            if (interface == 1) {
                if (data || d[3] || d[4] != 2 || d[5] != 10) return false;
                data = true;
            }
        } else if (d[1] == 0x24 && interface == 0) {
            if (n < 3) return false;
            if (d[2] == 6) unionDesc = n == 5 && d[3] == 0 && d[4] == 1;
            if (d[2] == 2) acm = n >= 4 && (d[3] & 2);
        } else if (d[1] == 5 && interface == 1) {
            if (n < 7 || (d[3] & 3) != 2 || !(d[2] & 15) ||
                !(d[4] | d[5]) || d[5] || d[4] > 64) return false;
            if (d[2] & 0x80) { out.bulkIn = d[2]; ++in; }
            else { out.bulkOut = d[2]; ++outCount; }
        }
        at += n;
    }
    return iad && comm && data && unionDesc && acm && in == 1 && outCount == 1;
}

} // namespace fujinet::io
