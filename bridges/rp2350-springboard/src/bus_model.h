#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { SB_ROM_CAPACITY = 65536, SB_CAPTURE_CAPACITY = 128 };
enum sb_cycle {
    SB_IDLE,
    SB_READ_CS0,
    SB_READ_CS1,
    SB_WRITE_CS0,
    SB_WRITE_CS1,
    SB_INVALID
};
enum sb_mode { SB_PASSIVE, SB_BENCH_ACTIVE, SB_PRISM_ROM };
enum {
    SB_REG_ID = 0,
    SB_REG_ABI = 2,
    SB_REG_SCRATCH = 4,
    SB_REG_READS = 6,
    SB_REG_WRITES = 8,
    SB_REG_STATUS = 10
};
enum { SB_ID = 0x4653, SB_ABI = 0x0001 };

struct sb_registers {
    uint16_t scratch, reads, writes;
};
struct sb_capture {
    uint32_t sample, sequence;
    uint8_t window, cs;
};
/* Single producer on core 1; debugger reads only after capture has stopped. */
struct sb_trace {
    struct sb_capture records[SB_CAPTURE_CAPACITY];
    uint32_t count, dropped;
};

/* Sampled on core 0; the first level establishes a baseline, not an edge. */
struct sb_reset_monitor {
    bool initialized, level;
    uint32_t asserted, released;
};
void sb_monitor_reset(struct sb_reset_monitor *monitor, bool level);

uint32_t sb_address(uint32_t low, uint32_t high);
enum sb_cycle sb_classify(uint32_t high);
uint16_t sb_rom_word(const uint8_t *rom, size_t size, uint32_t address);
uint32_t sb_crc32(const uint8_t *data, size_t size);
bool sb_load_rom(uint8_t *dest, size_t capacity, const uint8_t *source,
                 size_t size, uint32_t expected_crc);
void sb_register_reset(struct sb_registers *regs);
uint16_t sb_register_read(struct sb_registers *regs, uint32_t address);
bool sb_register_write(struct sb_registers *regs, uint32_t address,
                       uint16_t value);
void sb_trace_clear(struct sb_trace *trace);
bool sb_trace_full(const struct sb_trace *trace);
bool sb_trace_push(struct sb_trace *trace, struct sb_capture sample);
/* This base firmware intentionally refuses output-driving modes. */
bool sb_mode_supported(enum sb_mode mode);
