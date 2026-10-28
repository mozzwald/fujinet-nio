#pragma once
#include "rom_store.h"
#define RM_MAX_PAYLOAD 512u
#define RM_HEADER 16u
/* Dedicated CDC protocol. No character console or PacketLink parser runs here. */
enum rm_opcode {RM_STATUS=1,RM_QUIESCE,RM_BEGIN,RM_CHUNK,RM_FINISH,RM_READ,RM_ACTIVATE,RM_ROLLBACK,RM_RECOVERY,RM_RESUME,RM_ABORT,RM_VERIFY};
struct rm_frame {uint8_t data[RM_HEADER+RM_MAX_PAYLOAD];size_t used;};
/* Positive: complete CRC-valid request. -1: bad frame discarded. */
int rm_feed(struct rm_frame *,uint8_t);
size_t rm_encode(uint8_t *,uint8_t,uint32_t,const uint8_t *,size_t);
uint32_t rm_get(const uint8_t *);
void rm_put(uint8_t *,uint32_t);
struct rm_state {uint32_t flash_bytes,active,stopped,maintenance,core_held,served_source,served_length,served_crc32,fallback_reason,owner_fault;};
struct rm_backend {
 void *ctx;
 enum rs_error (*quiesce)(void *);
 enum rs_error (*resume)(void *);
 void (*load)(void *);
 struct rm_state (*state)(void *);
};
/* Dispatch only CRC-validated requests; used identically by hardware and tests. */
size_t rm_dispatch(struct rom_store *,struct rm_backend,const uint8_t *,uint8_t *);
