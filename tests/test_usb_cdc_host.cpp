#include "doctest.h"
#include "fujinet/io/usb_cdc_host_session.h"
#include "fujinet/diag/diagnostic_usb_host.h"
#include <vector>

using fujinet::io::UsbCdcHostSession;
using Bytes = std::vector<std::uint8_t>;

TEST_CASE("USB host RX remains readable and ordered across fragments and ring wrap") {
    UsbCdcHostSession s(16);
    CHECK_FALSE(s.available());
    const auto epoch = s.begin_open();
    Bytes a(20), b(12);
    for (unsigned i = 0; i < a.size(); ++i) a[i] = i;
    for (unsigned i = 0; i < b.size(); ++i) b[i] = 20 + i;
    REQUIRE(s.append(epoch, a.data(), a.size())); // May arrive while asserting DTR.
    CHECK_FALSE(s.available());
    REQUIRE(s.connected(epoch));
    std::uint8_t out[32]{};
    REQUIRE(s.read(out, 15) == 15);
    CHECK(s.available());
    REQUIRE(s.append(epoch, b.data(), b.size()));
    REQUIRE(s.read(out + 15, 17) == 17);
    for (unsigned i = 0; i < 32; ++i) CHECK(out[i] == i);
    CHECK_FALSE(s.available());
    CHECK(s.read(out, sizeof(out)) == 0);
}

TEST_CASE("USB host overflow drops entire session and rejects late callbacks") {
    UsbCdcHostSession s(8);
    const auto old = s.begin_open();
    REQUIRE(s.connected(old));
    Bytes full(16, 1);
    REQUIRE(s.append(old, full.data(), full.size()));
    CHECK_FALSE(s.append(old, full.data(), 1));
    CHECK(s.stats().overflows == 1);
    CHECK(s.state() == UsbCdcHostSession::State::Recovering);
    CHECK_FALSE(s.available());
    const auto next = s.begin_open();
    REQUIRE(s.connected(next));
    CHECK_FALSE(s.append(old, full.data(), 1));
    s.fail(old, true);
    CHECK(s.epoch() == next);
    CHECK(s.stats().disconnects == 0);
    REQUIRE(s.append(next, full.data(), 1));
    CHECK(s.available());
}

TEST_CASE("USB host accepts whole TX records only and never replays after failure") {
    UsbCdcHostSession s(16);
    const std::uint8_t record[16]{};
    CHECK_FALSE(s.enqueue(s.epoch(), record, sizeof(record)));
    auto epoch = s.begin_open();
    REQUIRE(s.connected(epoch));
    REQUIRE(s.enqueue(epoch, record, sizeof(record)));
    auto tx = s.take_tx();
    REQUIRE(tx.size() == 16);
    s.tx_complete(epoch, tx.size(), false);
    CHECK(s.stats().txFailures == 1);
    CHECK_FALSE(s.enqueue(epoch, record, 1));
    epoch = s.begin_open();
    REQUIRE(s.connected(epoch));
    CHECK(s.take_tx().empty());
    REQUIRE(s.enqueue(epoch, record, 1));
    CHECK_FALSE(s.enqueue(epoch, record, 1)); // Queue overrun loses the session.
    CHECK(s.take_tx().empty());
    CHECK(s.stats().txFailures == 2);
}

namespace {
Bytes config() {
    Bytes b = {
        9,2,0,0,5,1,0,0x80,125,
        8,11,0,2,2,2,0,0,
        9,4,0,0,1,2,2,0,4,
        5,0x24,0,0x20,1,
        5,0x24,1,0,1,
        4,0x24,2,6,
        5,0x24,6,0,1,
        7,5,0x81,3,8,0,16,
        9,4,1,0,2,10,0,0,0,
        7,5,0x06,2,64,0,0,
        7,5,0x86,2,64,0,0,
        8,11,2,2,2,2,0,0,
        9,4,2,0,1,2,2,0,5,
        9,4,3,0,2,10,0,0,0,
        9,4,4,0,0,0xff,0,1,6
    };
    b[2] = static_cast<std::uint8_t>(b.size());
    return b;
}
}
TEST_CASE("USB host identifies NIO IAD and discovers endpoints without claiming console/reset") {
    auto b = config();
    fujinet::io::SpringboardCdcFunction f;
    REQUIRE(fujinet::io::springboard_nio_cdc(b.data(), b.size(), f));
    CHECK(f.bulkIn == 0x86);
    CHECK(f.bulkOut == 0x06);
    SUBCASE("wrong IAD") { b[11] = 2; }
    SUBCASE("bad union") { b[44] = 3; }
    SUBCASE("missing OUT endpoint") { b[63] = 0x86; }
    SUBCASE("zero descriptor length") { b[9] = 0; }
    SUBCASE("truncated configuration") { b.pop_back(); }
    CHECK_FALSE(fujinet::io::springboard_nio_cdc(b.data(), b.size(), f));
}
TEST_CASE("USB host descriptor validation rejects every truncated prefix") {
    const auto b = config();
    fujinet::io::SpringboardCdcFunction f;
    for (std::size_t i = 0; i < b.size(); ++i)
        CHECK_FALSE(fujinet::io::springboard_nio_cdc(b.data(), i, f));
}
TEST_CASE("USB host rejects truncated NIO descriptors even with a matching configuration length") {
    const auto original = config();
    fujinet::io::SpringboardCdcFunction f;
    // The NIO function ends at byte 75; shorter configurations cannot contain
    // its complete pair of bulk endpoints, regardless of their total-length field.
    for (std::size_t size = 9; size < 75; ++size) {
        auto b = original;
        b.resize(size);
        b[2] = static_cast<std::uint8_t>(size);
        b[3] = 0;
        CHECK_FALSE(fujinet::io::springboard_nio_cdc(b.data(), b.size(), f));
    }
}
TEST_CASE("USB diagnostics expose connection counters without settings commands") {
    using namespace fujinet::diag;
    UsbHostSnapshot s;
    s.state = "connected";
    s.stats.connections = 3;
    s.stats.disconnects = 1;
    s.stats.errors = 2;
    s.stats.overflows = 1;
    s.stats.txFailures = 4;
    s.stats.rxBytes = 123;
    s.stats.txBytes = 456;
    s.epoch = 7;
    s.buffered = 8;
    s.bulkIn = 0x82;
    s.bulkOut = 0x02;
    s.lastError = 257;
    auto p = create_usb_host_diagnostic_provider([&] { return s; });
    std::vector<DiagCommandSpec> commands;
    p->list_commands(commands);
    REQUIRE(commands.size() == 1);
    CHECK(commands[0].name == "usb.status");
    CHECK(commands[0].safe);
    const auto result = p->execute({"usb.status", {"usb.status"}});
    CHECK(result.status == DiagStatus::Ok);
    for (const auto* line : {"state: connected", "function: 2e8a:0009 NIO CDC 0/1",
                           "bulk_in: 130", "bulk_out: 2", "epoch: 7", "buffered_rx: 8",
                           "connections: 3", "reconnects: 2", "disconnects: 1", "errors: 2",
                           "rx_overflows: 1", "tx_failures: 4", "rx_bytes: 123", "tx_bytes: 456", "last_error: 257"})
        CHECK(result.text.find(line) != std::string::npos);
    s.state = "waiting";
    CHECK(p->execute({"usb.status", {"usb.status"}}).text.find("state: waiting") != std::string::npos);
    CHECK(p->execute({"usb.set", {"usb.set"}}).status == DiagStatus::NotFound);
    CHECK(p->execute({"usb.status x", {"usb.status", "x"}}).status == DiagStatus::InvalidArgs);
}

TEST_CASE("USB host RX holds maximum records across many USB transfers") {
    constexpr std::size_t capacity = 65535;
    UsbCdcHostSession s(capacity + 10);
    const auto epoch = s.begin_open();
    REQUIRE(s.connected(epoch));
    Bytes bytes(2 * (capacity + 10));
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(i);
    for (std::size_t at = 0; at < bytes.size(); at += 512)
        REQUIRE(s.append(epoch, bytes.data() + at, std::min<std::size_t>(512, bytes.size() - at)));
    Bytes result(bytes.size());
    std::size_t at = 0;
    while (s.available()) at += s.read(result.data() + at, std::min<std::size_t>(257, result.size() - at));
    CHECK(result == bytes);
    CHECK(at == bytes.size());
}
