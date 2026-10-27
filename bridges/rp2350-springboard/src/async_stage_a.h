#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "visor_mailbox.h"
#include "cs1_registers.h"
struct sb_stage_a {
    struct sb_cs1_registers capture;
    volatile uint16_t *table;
    uint32_t accepted;
};
void sb_stage_a_init(struct sb_stage_a *, volatile uint16_t *);
bool sb_stage_a_retire(struct sb_stage_a *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
void sb_stage_a_fault(struct sb_stage_a *, uint16_t);
