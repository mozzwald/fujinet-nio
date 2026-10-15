#include "bus_model.h"
#include <string.h>

uint32_t sb_address(uint32_t low, uint32_t high) {
    /* high is the GPIO16..47 window. Skip GP39 between A18 and A19. */
    return (low & 0x0000fffeu) | ((high & 0x00700000u) >> 4) |
           ((high & 0x1f000000u) >> 5);
}

enum sb_cycle sb_classify(uint32_t high) {
    if (!(high & (1u << 29)))
        return SB_IDLE; /* /RESET asserted */
    const unsigned cs = high & 3u;
    if (cs == 3)
        return SB_IDLE;
    if (cs == 0)
        return SB_INVALID;
    const unsigned strobes = (high >> 2) & 3u;
    if (strobes == 3)
        return SB_IDLE;
    if (strobes == 0)
        return SB_INVALID;
    if (strobes == 2)
        return cs == 2 ? SB_READ_CS0 : SB_READ_CS1;
    return cs == 2 ? SB_WRITE_CS0 : SB_WRITE_CS1;
}

uint16_t sb_rom_word(const uint8_t *rom, size_t size, uint32_t address) {
    if (!rom || (address & 1u) || address >= size || size - address < 2)
        return 0xffff;
    return (uint16_t)((uint16_t)rom[address] << 8) | rom[address + 1];
}

uint32_t sb_crc32(const uint8_t *data, size_t size) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool sb_load_rom(uint8_t *dest, size_t capacity, const uint8_t *source,
                 size_t size, uint32_t expected_crc) {
    if (!dest || !source || !size || (size & 1) || size > capacity ||
        sb_crc32(source, size) != expected_crc)
        return false;
    memcpy(dest, source, size);
    return sb_crc32(dest, size) == expected_crc;
}

void sb_register_reset(struct sb_registers *regs) {
    memset(regs, 0, sizeof(*regs));
}
uint16_t sb_register_read(struct sb_registers *regs, uint32_t address) {
    uint16_t value;
    switch (address) {
    case SB_REG_ID:
        value = SB_ID;
        break;
    case SB_REG_ABI:
        value = SB_ABI;
        break;
    case SB_REG_SCRATCH:
        value = regs->scratch;
        break;
    case SB_REG_READS:
        value = regs->reads;
        break;
    case SB_REG_WRITES:
        value = regs->writes;
        break;
    case SB_REG_STATUS:
        value = 0;
        break; /* no active bus or peer */
    default:
        value = 0xffff;
        break;
    }
    ++regs->reads;
    return value;
}
bool sb_register_write(struct sb_registers *regs, uint32_t address,
                       uint16_t value) {
    if (address != SB_REG_SCRATCH)
        return false;
    regs->scratch = value;
    ++regs->writes;
    return true;
}
bool sb_trace_push(struct sb_trace *trace, struct sb_capture sample) {
    if (trace->count >= SB_CAPTURE_CAPACITY) {
        ++trace->dropped;
        return false;
    }
    trace->records[trace->count++] = sample;
    return true;
}
bool sb_mode_supported(enum sb_mode mode) { return mode == SB_PASSIVE; }
