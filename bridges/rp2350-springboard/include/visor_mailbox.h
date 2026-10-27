#ifndef FUJINET_VISOR_MAILBOX_H
#define FUJINET_VISOR_MAILBOX_H
/* Transport ABI 1.0. Byte offsets; ALL peripheral operations are aligned words.
 * Independent of NIO StatusCode. Never overlay a native C struct on the bus.
 * Canonical raw packet byte 0 occupies bits15:8; odd final low byte is zero.
 */
#define VMB_ABI_VERSION 0x0100u
#define VMB_ID_VALUE 0x464eu
#define VMB_CAP_STAGE_A 0x0001u
#define VMB_CAP_MAILBOX 0x0002u
#define VMB_ID 0x00u
#define VMB_VERSION 0x02u
#define VMB_CAPABILITIES 0x04u
#define VMB_STATUS 0x06u
#define VMB_GENERATION 0x08u
#define VMB_CONTROL 0x0au
#define VMB_SUBMIT 0x0cu
#define VMB_ACCEPTED 0x0eu
#define VMB_RESPONSE_SEQ 0x10u
#define VMB_ACK 0x12u
#define VMB_REQUEST_LENGTH 0x14u
#define VMB_RESPONSE_LENGTH 0x16u
#define VMB_ERROR 0x18u
#define VMB_PROBE 0x1au
#define VMB_PROBE_ACK 0x1cu
#define VMB_REQUEST 0x0100u
#define VMB_RESPONSE 0x1100u
#define VMB_CAPACITY 4096u
#define VMB_TABLE_BYTES 65536u
#define VMB_ST_CAN_STAGE 0x0001u
#define VMB_ST_BUSY 0x0002u
#define VMB_ST_READY 0x0004u
#define VMB_ST_ERROR 0x0008u
#define VMB_ST_FAULT 0x0010u
#define VMB_CTL_ABORT 1u
#define VMB_CTL_RESET 2u
#define VMB_ERR_NONE 0u
#define VMB_ERR_UNSUPPORTED 1u
#define VMB_ERR_SEQUENCE 2u
#define VMB_ERR_LENGTH 3u
#define VMB_ERR_OVERFLOW 4u
#define VMB_ERR_OWNERSHIP 5u
#define VMB_ERR_ABORTED 6u
#define VMB_ERR_CAPTURE 7u
/* Stage A: only PROBE is writable. Write next nonzero token then poll PROBE_ACK.
 * Initial ACK=0. Duplicate ACK is idempotent. Other tokens fail explicitly.
 * Reads may return an earlier published snapshot; compare the exact token.
 * At most ONE unacknowledged write; do not pipeline peripheral writes.
 * Stage B registers/windows are reserved, not advertised or implemented yet.
 */
#endif
