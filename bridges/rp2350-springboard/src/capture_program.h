#pragma once
/* Four input-only observers: PIO0 SM0/1 low window, PIO1 SM0/1 high window.
 * SM0 observes /CS0, SM1 /CS1. Records are separate streams, not paired cycles.
 */
#include <stdbool.h>

void sb_capture_program_init(bool enable);
