/* Functional test of the actual assembled reply kernel under qemu-arm.
 * RAM registers substitute for PIO/SIO: this tests ARM instructions, lookup,
 * predicates, stores and ABI, not peripheral timing or bus release. */
#include "cpu_reply.h"

static uint16_t words[32768];
static volatile uint32_t pio_regs[16], gpio_in;
static volatile bool stop_requested;
static unsigned cases;

static __attribute__((noreturn)) void finish(unsigned code) {
    register unsigned r0 __asm("r0") = code;
    register unsigned r7 __asm("r7") = 1; /* Linux ARM exit */
    __asm volatile("svc 0" : : "r"(r0), "r"(r7) : "memory");
    __builtin_unreachable();
}
#define CHECK(x) do { if (!(x)) finish(1); } while (0)

static void check_reply(uint32_t high, uint32_t low) {
    struct sb_cpu_request result = {0};
    const struct sb_cpu_bus bus = {pio_regs, &gpio_in, &stop_requested};
    pio_regs[1] = 0; /* RX ready */
    pio_regs[8] = high;
    pio_regs[4] = 0x12345678;
    gpio_in = low;
    const bool valid = (high & 0x2000000fu) == 0x2000000au;
    const uint16_t expected = high & 0x1f700000u ? 0xffff : words[(low & 0xfffeu) >> 1];
    CHECK(sb_rx_first_reply(&result, words, &bus));
    CHECK(result.high == high && result.low == low);
    CHECK(result.word == expected && result.valid == valid);
    CHECK(pio_regs[4] == (valid ? expected : 0x12345678u));
    ++cases;
}

static void run_tests(void) {
    for (unsigned i = 0; i < 32768; ++i)
        words[i] = (uint16_t)((i * 40503u) ^ (i >> 3) ^ 0xa55au);
    /* Exhaustive native table indices, with unrelated GPIO bits set. */
    for (unsigned i = 0; i < 32768; ++i)
        check_reply(0x2000000au, (i << 1) | 0xa5a50001u);
    /* Every high address line must produce FFFF, never wrap into the table. */
    for (unsigned bit = 0; bit < 32; ++bit) if ((1u << bit) & 0x1f700000u)
        for (unsigned i = 0; i < 256; ++i)
            check_reply(0x2000000au | (1u << bit), i * 254u);
    /* All RESET/control combinations, irrespective of coincident IRQ park. */
    pio_regs[12] = 1;
    for (unsigned reset = 0; reset < 2; ++reset)
        for (unsigned controls = 0; controls < 16; ++controls)
            check_reply((reset << 29) | controls, 0x2468);
    uint32_t seed = 0x692a4c1du;
    for (unsigned i = 0; i < 65536; ++i) {
        seed = seed * 1664525u + 1013904223u;
        check_reply(seed, seed ^ (seed >> 16));
    }
    /* Idle stop returns without touching RX, TX or the output record. A
     * request already present has priority, with PIO retaining cancellation. */
    struct sb_cpu_request result = {1, 2, 3, true};
    const struct sb_cpu_bus bus = {pio_regs, &gpio_in, &stop_requested};
    stop_requested = true;
    pio_regs[1] = 0x100;
    pio_regs[4] = 0x12345678;
    CHECK(!sb_rx_first_reply(&result, words, &bus));
    CHECK(result.high == 1 && result.low == 2 && result.word == 3 && result.valid);
    CHECK(pio_regs[4] == 0x12345678);
    check_reply(0x2000000au, 0);
    CHECK(cases == 100385);
}

__attribute__((noreturn)) void _start(void) {
    run_tests();
    finish(0);
}
