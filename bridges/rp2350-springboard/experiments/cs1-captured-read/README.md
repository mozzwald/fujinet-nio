# Captured CS1 read descriptor — host only

An input-only feasibility prototype following hardware verification of the
msc-003 passive-observer installation baseline. This is NOT an active CS1
responder, a mailbox, or a firmware candidate. No NIO or USB change. Never flash
the initializer ELF; it has no startup/vector table.

`capture.c` invokes the original ROM initializer, appends PIO0 words3..10 on
SM2/3, and uses PIO2 BASE0 for low-address capture and ending history. Original
PIO0 SM0/1/words0..2, all PIO1, ROM ARM kernel, GPIO mux/pads and clocks remain
unchanged. No Core0 code, IRQ handler or firmware CMake target is added.

Low capture triggers high capture via PIO2 NEXT IRQ7 → PIO0. High FIFO data is
published before PIO0 PREV IRQ2 acknowledges to PIO2. Only then is the low FIFO
word published and private PIO2 IRQ1 raised. One descriptor remains held until
explicit consumer acknowledgement (PIO2 IRQ3); no second request overwrites it.
The test consumer rejects ending/reset history, malformed snapshots and upper
address bits before using the latched low index. It never reads live GPIO for
address lookup. Address snapshots are not simultaneous; cancellation/qualification
and a future owner association remain essential.

PIO2 IRQ0 latches CS1/OE ending or WE assertion; IRQ4 is a sticky reset fault.
Reset fault requires reinitialization in this primitive. The input path cannot
release or enable outputs; all existing ROM output guards remain authoritative.
There is no corresponding PIO1 ownership token/response path. Do not feed these
records or replies into the ROM FIFOs.

Tests:

- `build_host.sh /absolute/new/output`: C input tests and differential libraries.
- `run_idle.py`: original334 compiled ROM schedules/cost profiles/assertions,
  with complete original-owner state projected (PIO0 SM0/1+words0..2, all PIO1,
  shared GPIO/IRQs); changed unused SMs/instruction slots checked separately.
- `run_transition.py`:32 cases compare identical ROM ARM/events/completions/
  projections during CS1 reads, writes, reset, cancellation and5..8clock gaps.
  Only private PIO0 IRQ7 is omitted during active-capture comparisons.
- `audit_initializer.py`: real ARM initializer original write prefix exact,
  extra PIO0 writes restricted to added instruction slots and SM2/3. No PIO1,
  baseline-SM or GPIO write may be added. Initializer ELF built separately
  with original pio_rearm_program.c and capture.c, entry sb_cs1_captured_init.

The122 C cases pass. Descriptor-ready is16 modeled clocks from read assertion
without an input synchronizer in that direct primitive test. The compiled ROM
harness retains its original two-clock input pipeline. These are distinct model
observations, not physical timing or a CS1 response deadline measurement.

The268-case full active-CS1 reply matrix is not applicable/passed by this input
primitive. The output service port, matched FIFO/epoch association, real event
consumer and active response timing remain blockers to a UF2.
