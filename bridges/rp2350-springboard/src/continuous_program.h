#pragma once
#include <stdbool.h>
enum { SB_CONT_MAIN=0, SB_CONT_WATCH=23, SB_CONT_CANCEL=26,
       SB_CONT_CLAMP=27, SB_CONT_RESET=28, SB_CONT_INSTRUCTIONS=30 };
/* Separate program. Guard SMs1/2 latch CS0/OE ending, SM3 latches RESET.
 * All three clamp data directions every clock until the owner resets the epoch.
 * Requires a new stopped/Hi-Z initialization; never patch a running baseline. */
void sb_continuous_program_init(bool enable);
