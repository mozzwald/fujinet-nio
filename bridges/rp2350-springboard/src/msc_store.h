#pragma once
/* One logical 512-byte sector per committed 4KiB record. Never maps firmware. */
#include "rom_store.h"
#define MS_BASE 0x100000u
#define MS_END 0xd00000u
#define MS_BLOCKS ((MS_END-MS_BASE)/RS_SECTOR)
#define MS_SECTORS 2048u
#define MS_BYTES (MS_SECTORS*512u)
#define MS_NONE UINT16_MAX
struct ms_disk {
    struct rs_io io;
    uint16_t map[MS_SECTORS];
    uint8_t live[MS_BLOCKS];
    uint64_t sequence;
    uint32_t cursor, writes;
    bool fault;
};
bool ms_init(struct ms_disk *, struct rs_io);
bool ms_read(struct ms_disk *, uint32_t, uint8_t [512]);
bool ms_write(struct ms_disk *, uint32_t, const uint8_t [512]);
bool ms_transfer(struct ms_disk *, bool, uint32_t, uint32_t, void *, size_t);
void ms_seed(uint32_t, uint8_t [512]);
