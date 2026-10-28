#include "write_capture.h"
/* These checks classify a diagnostic capture, not a committed peripheral
 * command. No low/high pairing is trusted unless exactly one selected WE pulse
 * occurred during the entire arm window. Multiple pulses reject the window.
 */
enum wc_result wc_validate(const struct wc_frame *f) {
    if (f->fifo_fault) return WC_FIFO;
    if (f->reset_edge) return WC_RESET;
    if (f->cs0_edge) return WC_CS0_ACTIVITY;
    if (f->oe_edge) return WC_READ_ACTIVITY;
    if (f->pulses > 1) return WC_MULTIPLE;
    if (f->pulses != 1 || f->low_depth != 2 || f->high_depth != 2 || f->end_depth != 1)
        return WC_INCOMPLETE;
    if (((f->low >> 16) & 15) != 5 || (f->high & 15) != 5 || !(f->high & (1u<<29)))
        return WC_BAD_SNAPSHOT;
    if (f->outcome || (f->low_end != 13 && f->low_end != 15) ||
        (f->high_end != 13 && f->high_end != 15)) return WC_CANCELLED;
    return WC_OK;
}
