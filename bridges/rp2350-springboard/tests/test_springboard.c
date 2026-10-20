#include "bus_model.h"
#include "capture_program.h"
#include <epio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e)                                                               \
    do {                                                                       \
        if (!(e)) {                                                            \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e);            \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

static void model_tests(void) {
    struct sb_reset_monitor reset = {0};
    sb_monitor_reset(&reset, false);
    CHECK(reset.initialized && !reset.level && reset.asserted == 0 &&
          reset.released == 0);
    sb_monitor_reset(&reset, false);
    CHECK(reset.asserted == 0 && reset.released == 0);
    sb_monitor_reset(&reset, true);
    sb_monitor_reset(&reset, true);
    CHECK(reset.level && reset.released == 1 && reset.asserted == 0);
    sb_monitor_reset(&reset, false);
    CHECK(!reset.level && reset.asserted == 1 && reset.released == 1);
    sb_monitor_reset(&reset, true);
    CHECK(reset.level && reset.asserted == 1 && reset.released == 2);
    CHECK(sb_address(0x00002468, 0x03500000) == 0x001d2468);
    CHECK(sb_address(0x0000fffe, 0x1f700000) == 0x00fffffe);
    CHECK(sb_address(1, 0x00800000) == 0); /* IRQ and GP39 are not address */
    for (unsigned bit = 1; bit <= 23; ++bit) {
        uint32_t low = bit <= 15 ? 1u << bit : 0;
        uint32_t high = bit >= 16 ? 1u << (bit <= 18 ? bit + 4 : bit + 5) : 0;
        CHECK(sb_address(low, high) == (1u << bit));
    }
    CHECK(sb_classify(0x2000000a) == SB_READ_CS0);
    CHECK(sb_classify(0x20000009) == SB_READ_CS1);
    CHECK(sb_classify(0x20000006) == SB_WRITE_CS0);
    CHECK(sb_classify(0x20000005) == SB_WRITE_CS1);
    CHECK(sb_classify(0x0000000a) == SB_IDLE);
    CHECK(sb_classify(0x2000000f) == SB_IDLE);
    CHECK(sb_classify(0x20000008) == SB_INVALID);
    CHECK(sb_classify(0x20000002) == SB_INVALID);
    const uint8_t rom[] = {0x12, 0x34, 0xab, 0xcd};
    CHECK(sb_rom_word(rom, 4, 0) == 0x1234);
    CHECK(sb_rom_word(rom, 4, 2) == 0xabcd);
    CHECK(sb_rom_word(rom, 4, 1) == 0xffff);
    CHECK(sb_rom_word(rom, 4, 4) == 0xffff);
    CHECK(sb_rom_word(rom, 3, 2) == 0xffff);
    CHECK(sb_rom_word(rom, 4, UINT32_MAX) == 0xffff);
    CHECK(sb_crc32((const uint8_t *)"123456789", 9) == 0xcbf43926);
    uint8_t copy[4] = {0};
    CHECK(sb_load_rom(copy, 4, rom, 4, sb_crc32(rom, 4)));
    CHECK(memcmp(copy, rom, 4) == 0);
    CHECK(!sb_load_rom(copy, 3, rom, 4, sb_crc32(rom, 4)));
    CHECK(!sb_load_rom(copy, 4, rom, 3, sb_crc32(rom, 3)));
    CHECK(!sb_load_rom(copy, 4, rom, 4, 0));
    struct sb_registers regs;
    sb_register_reset(&regs);
    CHECK(sb_register_read(&regs, 0) == 0x4653);
    CHECK(sb_register_read(&regs, 2) == 1);
    CHECK(sb_register_write(&regs, 4, 0xa55a));
    CHECK(sb_register_read(&regs, 4) == 0xa55a);
    CHECK(!sb_register_write(&regs, 0, 0));
    CHECK(!sb_register_write(&regs, 5, 0));
    sb_register_reset(&regs);
    CHECK(regs.scratch == 0 && regs.reads == 0 && regs.writes == 0);
    struct sb_trace trace = {0};
    for (unsigned i = 0; i < 128; ++i)
        CHECK(sb_trace_push(&trace, (struct sb_capture){.sample = i}));
    CHECK(!sb_trace_push(&trace, (struct sb_capture){.sample = 999}));
    CHECK(trace.count == 128 && trace.dropped == 1 &&
          trace.records[127].sample == 127);
    CHECK(sb_trace_full(&trace));
    sb_trace_clear(&trace);
    CHECK(!sb_trace_full(&trace) && trace.count == 0 && trace.dropped == 0);
    CHECK(sb_trace_push(&trace, (struct sb_capture){.sample = 42}));
    CHECK(trace.count == 1 && trace.records[0].sample == 42);
    CHECK(sb_mode_supported(SB_PASSIVE));
    CHECK(!sb_mode_supported(SB_BENCH_ACTIVE) &&
          !sb_mode_supported(SB_PRISM_ROM));
}

/* Each step is longer than the observer's polling loop. These are behavioral
 * tests, not claims about real input synchronization or minimum strobe width. */
static void drive(epio_t *e, uint64_t value) {
    epio_drive_gpios_ext(e, (UINT64_C(1) << 48) - 1, value);
    epio_step_cycles(e, 50);
}
static void expect_empty(epio_t *e) {
    for (unsigned block = 0; block < 2; ++block)
        for (unsigned sm = 0; sm < 2; ++sm)
            CHECK(epio_rx_fifo_depth(e, block, sm) == 0);
}
static void expect_read(epio_t *e, unsigned cs, uint64_t value) {
    CHECK(epio_rx_fifo_depth(e, 0, cs) == 1);
    CHECK(epio_rx_fifo_depth(e, 1, cs) == 1);
    CHECK(epio_pop_rx_fifo(e, 0, cs) == (uint32_t)value);
    CHECK(epio_pop_rx_fifo(e, 1, cs) == (uint32_t)(value >> 16));
    expect_empty(e);
}
static void pio_tests(void) {
    sb_capture_program_init(false);
    epio_t *e = epio_from_apio();
    CHECK(e != NULL);
    drive(e, UINT64_C(0x00002000000f2468));
    drive(e, UINT64_C(0x00002000000a2468));
    expect_empty(e); /* Start stopped: bus activity cannot fill the trace. */
    epio_free(e);
    sb_capture_program_init(true);
    e = epio_from_apio();
    CHECK(e != NULL);
    for (unsigned pin = 0; pin <= 46; ++pin) {
        if (pin == 39) continue;
        CHECK(epio_get_gpio_input_only(e, pin));
        for (unsigned block = 0; block < 3; ++block)
            CHECK(!epio_block_can_control_gpio_output(e, block, pin));
    }
    /* Initially selected: require a release before accepting a read. */
    drive(e, UINT64_C(0x00002000000a2468));
    expect_empty(e);
    drive(e, UINT64_C(0x00002000000f2468));
    drive(e, UINT64_C(0x00002000000e2468)); /* CS before OE */
    expect_empty(e);
    drive(e, UINT64_C(0x00002000000a2468));
    expect_read(e, 0, UINT64_C(0x00002000000a2468));
    epio_step_cycles(e, 50);
    expect_empty(e); /* Held CS/OE gives only one sample. */
    drive(e, UINT64_C(0x00002000000e2468));
    drive(e, UINT64_C(0x00002000000a2468));
    expect_empty(e); /* OE pulse again under held CS is not a new CS cycle. */
    drive(e, UINT64_C(0x00002000000f2468));
    drive(e, UINT64_C(0x00002000000b2468)); /* OE before CS */
    expect_empty(e);
    drive(e, UINT64_C(0x00002000000a2468));
    expect_read(e, 0, UINT64_C(0x00002000000a2468));
    /* Abandon select before OE, then unrelated OE: no stale transaction. */
    drive(e, UINT64_C(0x00002000000f2468));
    drive(e, UINT64_C(0x00002000000e2468));
    drive(e, UINT64_C(0x00002000000f2468));
    drive(e, UINT64_C(0x00002000000b2468));
    expect_empty(e);
    drive(e, UINT64_C(0x00002000000f2468));
    drive(e, UINT64_C(0x0000200000062468)); /* write */
    expect_empty(e);
    drive(e, UINT64_C(0x00002000000f2468));
    drive(e, UINT64_C(0x0000200000022468)); /* OE and WE both low */
    expect_empty(e);
    drive(e, UINT64_C(0x00002000000f2468));
    drive(e, UINT64_C(0x0000200000082468)); /* both selects low */
    expect_empty(e);
    drive(e, UINT64_C(0x00001234000f5678));
    drive(e, UINT64_C(0x0000123400095678));
    expect_read(e, 1, UINT64_C(0x0000123400095678));
    /* Saturation remains Hi-Z; reconfiguration starts from fresh state. */
    for (unsigned i = 0; i < 8; ++i) {
        drive(e, UINT64_C(0x00001234000f5678));
        drive(e, UINT64_C(0x0000123400095678));
    }
    CHECK(epio_rx_fifo_depth(e, 0, 1) == 4);
    CHECK(epio_rx_fifo_depth(e, 1, 1) == 4);
    CHECK(epio_get_gpio_output_control(e, 0) == 0);
    CHECK(epio_get_gpio_output_control(e, 1) == 0);
    epio_free(e);
    sb_capture_program_init(true);
    e = epio_from_apio();
    drive(e, UINT64_C(0x00002000000f2468));
    expect_empty(e);
    drive(e, UINT64_C(0x00002000000a2468));
    expect_read(e, 0, UINT64_C(0x00002000000a2468));
    epio_free(e);
}

int main(void) {
    model_tests();
    pio_tests();
    puts("Springboard model and passive PIO tests passed");
    return 0;
}
