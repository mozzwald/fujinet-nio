# Unified bus feasibility — host only

This is an architecture experiment, NOT a firmware target. Never flash its ELF.
Production PIO/ARM/ROM sources and stored images are unchanged.

`probe_program.c` copies the existing asynchronous capture fixture solely to
exercise a smaller, CS0-first PIO1 admission/completion program. It is not a
claim that A2's guards/startup are hardware qualified. The original PIO1 program
in `src/pio_rearm_program.c` remains the reference, not this fixture.

`workers.S` retains the persistent request/completion loop and ROM lookup,
validates controls and sticky cancellation before lookup, and branches off the
normal ROM path for cached CS1 register reads. Core1 does no write processing.
`run_probe.py` uses the original strict mapped-register ARM/PIO harness. It
preserves failures and exits nonzero if any case fails. Read-only cases disable
the harness's Core0 polling; write cases use its existing functional retirement
model (not compiled Core0 scheduling). Persistent kernel `posted` counts valid
snapshots; actual TX event count additionally includes forced-cancel dummies.
Both accounting contracts are checked explicitly.

`compare_timing.py` applies the same synthetic costs to the frozen msc-003 ELF
and the experimental ELF. It normalizes PIO enable timestamps to the end of a
modeled clock (the baseline native event log uses the start). Both use ordinary
mapped registers. These costs are not Cortex-M33 timing bounds.

Full results and reproduction commands are documented in the private plan:
`docs/visor/UNIFIED_SPRINGBOARD_BUS_ARCHITECTURE.md`, p4m-011.
The final 268-case matrix passes237/fails31; no acceptance gate is passed.
