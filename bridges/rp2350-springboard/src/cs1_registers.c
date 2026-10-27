#include "cs1_registers.h"
#include "bus_model.h"
#include <string.h>

void sb_cs1_register_reset(struct sb_cs1_registers *state) {
    memset(state,0,sizeof(*state));
    state->reserved=0xffff;
}
uint16_t sb_cs1_register_read(const struct sb_cs1_registers *state, uint32_t address) {
    if (state->fault) return 0xffff;
    switch (address) {
    case SB_CS1_ID_OFFSET: return SB_CS1_ID;
    case SB_CS1_ABI_OFFSET: return SB_CS1_ABI;
    case SB_CS1_SCRATCH_OFFSET: return state->scratch;
    default: return 0xffff;
    }
}
static bool reject(struct sb_cs1_registers *state, enum sb_cs1_fault fault) {
    state->fault=fault; ++state->rejected; return false;
}
bool sb_cs1_write_record(struct sb_cs1_registers *state,
                         uint32_t low, uint32_t high,
                         uint32_t epoch, uint32_t outcome, uint32_t faults) {
    if (state->fault) return false;
    if (faults & SB_CS1_FAULT_FIFO) return reject(state,SB_CS1_FIFO_FAULT);
    if (faults & SB_CS1_FAULT_CONTROL) return reject(state,SB_CS1_CONTROL_FAULT);
    if (epoch!=state->next_epoch) return reject(state,SB_CS1_EPOCH_MISMATCH);
    if (outcome!=0 && outcome!=UINT32_MAX) return reject(state,SB_CS1_OUTCOME_MALFORMED);
    if (((low>>16)&15)!=5 || (high&15)!=5)
        return reject(state,SB_CS1_CAPTURE_MISMATCH);
    if (!(high&(1u<<29))) return reject(state,SB_CS1_RESET_CAPTURE);
    --state->next_epoch; ++state->captured;
    /* Ending disposition was latched at WE ending by PIO, before the CPU
     * dequeued anything. Later idle control/address values are not evidence.
     * A cancelled write is retired without poisoning the following epoch.
     */
    if (outcome) { ++state->rejected; return true; }
    uint32_t address=sb_address(low,high);
    if (address==SB_CS1_SCRATCH_OFFSET) {
        state->scratch=(uint16_t)(high>>4); ++state->committed;
    } else ++state->ignored;
    return true;
}
