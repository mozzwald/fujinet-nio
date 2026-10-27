#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Phase1 transport diagnostics, not NIO service commands. */
enum { SB_CS1_ID_OFFSET=0, SB_CS1_ABI_OFFSET=2, SB_CS1_SCRATCH_OFFSET=4,
       SB_CS1_RESERVED_OFFSET=6, SB_CS1_ID=0x4653, SB_CS1_ABI=1 };
enum sb_cs1_fault {
    SB_CS1_HEALTHY, SB_CS1_CAPTURE_MISMATCH, SB_CS1_RESET_CAPTURE,
    SB_CS1_WRITE_END_UNQUALIFIED, SB_CS1_ADDRESS_CHANGED, SB_CS1_FIFO_FAULT,
    SB_CS1_EPOCH_MISMATCH, SB_CS1_OUTCOME_MALFORMED, SB_CS1_CONTROL_FAULT
};
struct sb_cs1_registers {
    uint16_t scratch, reserved;
    uint32_t fault; /* fixed ABI: ARM compilers may use short enums */
    uint32_t captured, committed, ignored, rejected, next_epoch;
};
void sb_cs1_register_reset(struct sb_cs1_registers *state);
uint16_t sb_cs1_register_read(const struct sb_cs1_registers *state, uint32_t address);
bool sb_cs1_write_record(struct sb_cs1_registers *state,
                         uint32_t start_low, uint32_t start_high,
                         uint32_t epoch, uint32_t outcome, uint32_t faults);
/* true means a well-formed epoch was retired (including cancellation).
 * outcome is 0=complete, UINT32_MAX=cancelled. Fault bits must be supplied
 * from sticky hardware FDEBUG.RXSTALL and PIO2 IRQ5 BEFORE any FIFO drain.
 * Clear faults/epoch/FIFOs together only during a coordinated stopped reset.
 */
enum { SB_CS1_FAULT_CONTROL=1, SB_CS1_FAULT_FIFO=2 };
