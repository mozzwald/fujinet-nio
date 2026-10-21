#pragma once
#include <stdbool.h>

#ifndef SB_ROM_DRIVE_MARKER
#define SB_ROM_DRIVE_MARKER 0
#endif
/* Diagnostic only: GP39 is the onboard LED, never a Springboard signal.
 * Mandatory one-bit side-set mirrors the data PINDIRS window without adding
 * instructions. Core 0 must not use the LED when this option is enabled. */
#define SB_ROM_DRIVE_MARKER_PIN 39

/* Experimental CS0 only. PIO1/SM0, GPIOBASE16; IRQ0 is a parked,
 * already-Hi-Z completion. Clear BOTH FIFOs before acknowledging IRQ0.
 * IRQ1 records that output directions were enabled in this cycle; sample it
 * at completion and clear it before acknowledging IRQ0. It is not proof that
 * the CPU received data before its sampling deadline.
 * No automatic retry: a delayed CPU reply cannot escape into a new cycle.
 * This sampled guard is NOT a combinational output-enable circuit. */
void sb_rom_program_init(bool enable);
