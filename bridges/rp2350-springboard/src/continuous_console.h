#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct sb_continuous_status {
    bool rom_ready, ready, stopped, arm_pending, active, counters_final;
    uint32_t run, requests, posted, completed, attempted, cancelled, empty_ends;
};
/* Three bounded messages on separate core0 console ticks. Never block the
 * response core to make room in the nonblocking USB transmit buffer. */
int sb_continuous_status_line(char *buffer, size_t size, unsigned page,
                              const struct sb_continuous_status *status);
