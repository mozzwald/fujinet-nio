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
    CHECK(sb_mode_supported(SB_PASSIVE));
    CHECK(!sb_mode_supported(SB_BENCH_ACTIVE) &&
          !sb_mode_supported(SB_PRISM_ROM));
}

static void pio_tests(void) {
    sb_capture_program_init();
    epio_t *e = epio_from_apio();
    CHECK(e != NULL);
    for (unsigned pin = 0; pin <= 46; ++pin) {
        if (pin == 39)
            continue;
        CHECK(epio_get_gpio_input_only(e, pin));
        for (unsigned block = 0; block < 3; ++block)
            CHECK(!epio_block_can_control_gpio_output(e, block, pin));
    }
    const uint64_t mask = (UINT64_C(1) << 48) - 1;
    /* Start already selected: guards must not invent a first transaction. */
    epio_drive_gpios_ext(e, mask, UINT64_C(0x00000002000e2468));
    epio_step_cycles(e, 20);
    CHECK(epio_rx_fifo_depth(e, 0, 0) == 0);
    epio_drive_gpios_ext(e, mask, UINT64_C(0x00000002000f2468));
    epio_step_cycles(e, 20);
    epio_drive_gpios_ext(e, mask, UINT64_C(0x00000002000e2468));
    epio_step_cycles(e, 20);
    CHECK(epio_rx_fifo_depth(e, 0, 0) == 1);
    CHECK(epio_rx_fifo_depth(e, 1, 0) == 1);
    CHECK(epio_pop_rx_fifo(e, 0, 0) == 0x000e2468);
    CHECK(epio_pop_rx_fifo(e, 1, 0) == 0x0002000e);
    CHECK(epio_rx_fifo_depth(e, 0, 1) == 0);
    epio_step_cycles(e, 40);
    CHECK(epio_rx_fifo_depth(e, 0, 0) == 0); /* held select */
    /* /CS1 has its own SM and high-window mapping. */
    epio_drive_gpios_ext(e, mask, UINT64_C(0x00001234000f5678));
    epio_step_cycles(e, 10);
    epio_drive_gpios_ext(e, mask, UINT64_C(0x00001234000d5678));
    epio_step_cycles(e, 10);
    CHECK(epio_pop_rx_fifo(e, 0, 1) == 0x000d5678);
    CHECK(epio_pop_rx_fifo(e, 1, 1) == 0x1234000d);
    /* Saturation cannot enable data outputs; no cross-window pairing assumed.
     */
    for (unsigned i = 0; i < 8; ++i) {
        epio_drive_gpios_ext(e, mask, UINT64_C(0x00001234000f5678));
        epio_step_cycles(e, 10);
        epio_drive_gpios_ext(e, mask, UINT64_C(0x00001234000d5678));
        epio_step_cycles(e, 10);
    }
    CHECK(epio_rx_fifo_depth(e, 0, 1) == 4);
    CHECK(epio_rx_fifo_depth(e, 1, 1) == 4);
    CHECK(epio_get_gpio_output_control(e, 0) == 0);
    CHECK(epio_get_gpio_output_control(e, 1) == 0);
    epio_free(e);
}

int main(void) {
    model_tests();
    pio_tests();
    puts("Springboard model and passive PIO tests passed");
    return 0;
}
