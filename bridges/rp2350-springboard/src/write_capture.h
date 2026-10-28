#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Experiment3 capture-only observer. No register/mailbox side effects.
 * One arm window permits exactly ONE selected WE pulse. Input-only machines
 * halt after their first frame; the independent pulse counter detects extras.
 * Low address/data are immutable hardware snapshots, never live CPU samples.
 */
enum { WC_HIGH_START=7, WC_HIGH_WORDS=21, WC_LOW_START=0, WC_LOW_WORDS=14,
       WC_END_START=14, WC_CS_START=20, WC_COUNT_START=23, WC_WORDS=32 };
struct wc_frame {
    uint32_t low, low_end, high, high_end, outcome, pulses;
    uint32_t low_depth, high_depth, end_depth;
    bool reset_edge, cs0_edge, oe_edge, fifo_fault;
};
enum wc_result { WC_OK, WC_INCOMPLETE, WC_MULTIPLE, WC_RESET,
                 WC_CS0_ACTIVITY, WC_CANCELLED, WC_BAD_SNAPSHOT, WC_FIFO, WC_READ_ACTIVITY,
                 WC_EXPIRED, WC_NOT_READY };
enum wc_result wc_validate(const struct wc_frame *f);
void sb_write_capture_program_init(bool enable);
