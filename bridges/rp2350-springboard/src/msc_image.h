#pragma once
#include "msc_store.h"
#define MI_FILE_CAP (32u+RS_CAPACITY)
enum mi_result {MI_OK,MI_ABSENT,MI_MEDIA,MI_FAT,MI_FILE,MI_PACKAGE,MI_ROM,MI_STORAGE};
struct mi_image {uint32_t version,length,crc;};
enum mi_result mi_package(uint8_t *,size_t,struct mi_image *);
enum mi_result mi_load(struct ms_disk *,uint8_t [MI_FILE_CAP],struct mi_image *);
enum mi_result mi_promote(struct rom_store *,const uint8_t *,struct mi_image);
bool mi_rom(const uint8_t *,size_t);
