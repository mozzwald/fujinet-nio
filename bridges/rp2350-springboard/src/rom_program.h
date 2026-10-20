#pragma once
#include <stdbool.h>

/* Experimental CS0 only. PIO1/SM0, GPIOBASE16; IRQ0 is a parked,
 * already-Hi-Z completion. Clear BOTH FIFOs before acknowledging IRQ0.
 * No automatic retry: a delayed CPU reply cannot escape into a new cycle.
 * This sampled guard is NOT a combinational output-enable circuit. */
void sb_rom_program_init(bool enable);
