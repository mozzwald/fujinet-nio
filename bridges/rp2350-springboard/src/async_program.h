#pragma once
#include <stdbool.h>

/* Separate Phase4 write-pairing candidate; baseline program remains unchanged.
 * PIO1 RX0 alternates request/completion exactly as the frozen responder.
 * PIO0 RX2 and PIO2 RX1 each publish one active-write snapshot.
 * PIO2 RX2 publishes epoch/outcome, independently latched at WE ending.
 */
enum {
    SB_CS1_READ_SNAPSHOT = 8,
    SB_CS1_READ_PULL = 9,
    SB_CS1_READ_PRELOAD = 10,
    SB_CS1_READ_ENABLE = 16,
    SB_CS1_READ_COMPLETE = 23,
    SB_CS1_READ_WORDS = 29,
    SB_CS1_LOW_SM = 2,
    SB_CS1_HIGH_SM = 1,
    SB_CS1_LOW_START = 19,
    SB_CS1_HIGH_START = 6,
    SB_CS1_WRITE_WORDS = 13,
    SB_CS1_HIGH_WORDS = 14,
    SB_CS1_END_SM = 2,
    SB_CS1_CANCEL_SM = 3,
    SB_CS1_END_START = 20,
    SB_CS1_CANCEL_START = 27,
};
void sb_async_program_init(bool enable);
