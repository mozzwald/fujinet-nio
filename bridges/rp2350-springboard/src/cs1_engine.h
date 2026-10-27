#pragma once

/* Shared C/assembly offsets, never an on-wire NIO command structure. */
#define SB_CS1_SERVICE 24
#define SB_CS1_LOW_PIO 0
#define SB_CS1_HIGH_PIO 4
#define SB_CS1_REGS 12
#define SB_CS1_FAULT 16
#define SB_CS1_CAPTURED 20
#define SB_CS1_COMMITTED 24
#define SB_CS1_IGNORED 28
#define SB_CS1_REJECTED 32
#define SB_CS1_NEXT_EPOCH 36
#define SB_CS1_PHASE 40
#define SB_CS1_LOW 44
#define SB_CS1_HIGH 48
#define SB_CS1_EPOCH 52
#define SB_CS1_OUTCOME 56
#define SB_CS1_ENGINE_PHASE 64
#define SB_CS1_ENGINE_VALUES 32
#define SB_CS1_ENGINE_SCRATCH 36
#define SB_CS1_DRAIN_BUDGET 84
#define SB_CS1_ASM_FIFO_FAULT 5
#define SB_CS1_ASM_EPOCH_FAULT 6
#define SB_CS1_ASM_OUTCOME_FAULT 7
#define SB_CS1_ASM_CONTROL_FAULT 8

#ifndef __ASSEMBLER__
#include "persistent_reply.h"
#include "cs1_registers.h"
_Static_assert(SB_CS1_FIFO_FAULT==SB_CS1_ASM_FIFO_FAULT,"FIFO fault ABI");
_Static_assert(SB_CS1_EPOCH_MISMATCH==SB_CS1_ASM_EPOCH_FAULT,"epoch fault ABI");
_Static_assert(SB_CS1_OUTCOME_MALFORMED==SB_CS1_ASM_OUTCOME_FAULT,"outcome fault ABI");
_Static_assert(SB_CS1_CONTROL_FAULT==SB_CS1_ASM_CONTROL_FAULT,"control fault ABI");
struct sb_cs1_service {
    volatile uint32_t *low_pio, *high_pio;
    uint16_t id, abi;
    struct sb_cs1_registers regs;
    /* 0=start,1=partial start,2=reserved (fault),3=epoch checked,4=outcome retained.
     * phase survives read-priority yields; no FIFO is consumed twice. */
    uint32_t phase, low, high, epoch, outcome;
};
struct sb_cs1_engine {
    struct sb_persistent_totals totals;
    struct sb_cs1_service service;
    uint32_t drain_budget;
    uint32_t invalid_samples;
    uint32_t completed_msb; /* upper two completion bits; lower30 are packed in R12 */
};
_Static_assert(offsetof(struct sb_cs1_engine,service)==SB_CS1_SERVICE,"engine ABI");
_Static_assert(offsetof(struct sb_cs1_service,regs)==SB_CS1_REGS,"register ABI");
_Static_assert(offsetof(struct sb_cs1_service,phase)==SB_CS1_PHASE,"worker ABI");
_Static_assert(offsetof(struct sb_cs1_service,outcome)==SB_CS1_OUTCOME,"outcome ABI");
_Static_assert(offsetof(struct sb_cs1_service,regs.fault)==SB_CS1_FAULT,"fault ABI");
_Static_assert(offsetof(struct sb_cs1_service,regs.next_epoch)==SB_CS1_NEXT_EPOCH,"epoch ABI");
_Static_assert(sizeof(struct sb_cs1_service)==60,"service ABI");
_Static_assert(offsetof(struct sb_cs1_engine,drain_budget)==84,"drain ABI");
_Static_assert(offsetof(struct sb_cs1_engine,invalid_samples)==88,"invalid counter ABI");
_Static_assert(offsetof(struct sb_cs1_engine,completed_msb)==92,"carry ABI");
_Static_assert(sizeof(struct sb_cs1_engine)==96,"engine size");
void sb_cs1_reply(struct sb_cs1_engine *, const uint16_t *, const struct sb_cpu_bus *);
#endif
