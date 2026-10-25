#pragma once
#include <stdbool.h>
#include <stdint.h>

/* RP2350 PIO FSTAT: RXEMPTY[11:8], TXEMPTY[27:24]. A short restart
 * never drains FIFOs, so any outstanding word requires the full cleanup.
 * IRQ1 proves the main SM consumed its reply and commanded output enable;
 * it does not prove valid electrical data. IRQ7 is the host-stop latch. */
#define SB_CONT_ALL_FIFOS_EMPTY UINT32_C(0x0f000f00)
static inline bool sb_cont_fast_rearm_allowed(bool have, bool valid,
                                             uint32_t irq, uint32_t fstat) {
    return have && valid && (irq & 3u)==3u && !(irq & 0x80u) &&
        (fstat & SB_CONT_ALL_FIFOS_EMPTY)==SB_CONT_ALL_FIFOS_EMPTY;
}
