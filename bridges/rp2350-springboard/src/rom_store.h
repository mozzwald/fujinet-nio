#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RS_FLASH_BYTES 0x1000000u
#define RS_FIRMWARE_LIMIT 0x100000u
#define RS_CAPACITY 65536u
#define RS_SECTOR 4096u
#define RS_PAGE 256u
#define RS_SLOT0 0xfc0000u
#define RS_STRIDE 0x11000u
#define RS_META0 0xfe2000u
#define RS_RECOVERY 2u
#define RS_SCHEMA 1u
enum rs_error {RS_OK,RS_FRAME,RS_STATE,RS_RANGE,RS_CRC,RS_VERSION,RS_IO,RS_UNSAFE,RS_CONFLICT};
struct rs_image {uint32_t valid,version,length,crc;};
struct rs_record {uint32_t valid,generation,slot,version,length,crc;};
struct rs_io {
 void *ctx;
 bool (*read)(void *,uint32_t,void *,size_t);
 bool (*erase)(void *,uint32_t);
 bool (*program)(void *,uint32_t,const uint8_t *);
 bool (*safe)(void *);
};
struct rom_store {
 struct rs_io io;struct rs_image images[2];struct rs_record records[2];
 bool fault;uint32_t latest,upload,nonce,received;struct rs_image pending;
};
uint32_t rs_crc(const void *,size_t);
void rs_init(struct rom_store *,struct rs_io);
uint32_t rs_selected(const struct rom_store *);
const struct rs_image *rs_active(const struct rom_store *);
enum rs_error rs_begin(struct rom_store *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
enum rs_error rs_chunk(struct rom_store *,uint32_t,uint32_t,const uint8_t *,size_t);
enum rs_error rs_finish(struct rom_store *,uint32_t);
enum rs_error rs_verify(struct rom_store *,uint32_t);
enum rs_error rs_activate(struct rom_store *,uint32_t);
enum rs_error rs_rollback(struct rom_store *);
enum rs_error rs_read(struct rom_store *,uint32_t,uint32_t,void *,size_t);
void rs_abort(struct rom_store *);
bool rs_copy_active(struct rom_store *,uint8_t *,size_t);
