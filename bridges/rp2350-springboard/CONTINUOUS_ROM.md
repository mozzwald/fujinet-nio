# Experimental continuous ROM owner

Separate from RX-first-v2 bounded firmware. No physical qualification yet.
Build a verified ROM with scripts/build-continuous.sh ROM MANIFEST OUTPUTDIR
(all three paths absolute). This creates springboard_rom_continuous.uf2/elf;
it does not flash or arm. Preserve the known-good bounded firmware first.

Boot is disarmed and Hi-Z. Console commands: q identity/config; s state/counters;
R/Z/F select CPU ROM/zero/ones while stopped; b starts indefinite service; x stops.
Require rom_ready1, ready1, stopped0, arm_pending0, active1 before a manual read.
Startup waits for inactive CS0/OE and released RESET. limit is always0, meaning
no transaction budget. A separate collector must explicitly identify version1,
ROM word count/CRC and150000kHz, and confirm stop after the Palm saves its result.
No baseline console identity or bounded budget is emulated.

Core1 owns the RX/TX FIFOs and epoch resets. Main SM0 preloads data while Hi-Z,
requalifies controls/RESET, then commands direction enable. CS0/OE/RESET guard
SM1/2/3 independently latch endings and repeatedly clamp all data directions;
the highest-numbered SM wins simultaneous PIO direction writes. Optional guard
side-set clears GP39 when clamping, without changing the marker during WAITs.
After an ending, the owner stops main first, forces its direction low, disables
all SMs, flushes/restarts every FIFO/SM, clears IRQs and starts a fresh epoch.
Queued responses cannot survive this reset. It may capture a currently selected
next read as a new request; missed cycles during finite rearm time are possible.

The unchanged ARM RX-first kernel waits on an IRQ-register byte instead of the
old SRAM stop byte. IRQ0 cancellation wakes an empty RX wait; IRQ7 wakes host
stop. This changes peripheral traffic and does not preserve whole-path timing
merely because the kernel instructions are identical. Diagnostics run after TX.
Arbitrary/repeated aligned addresses below64KiB use the native SRAM word table;
unused/out-of-range words returnFFFF. There is no sequential-address assertion.

Host stop first forces all16 data pad OE overrides low, then forces SM3 into its
sticky clamp and sets IRQ7. The pad override survives a concurrent owner restart
and is cleared only at a new explicit arm, after all SMs have been parked. This
is a stop-only operation, not instrumentation on the response path. A stall on
core1 cannot defeat autonomous PIO release. A total CPU stall cannot receive a
new host command; pins still follow PIO end/reset qualification.

Counters: requests=CPU snapshots popped; posted=TX publications; completed=ended
epochs; attempted=main reached its enable IRQ, **not proof of actual pad drive**;
cancelled=ended without that IRQ; empty_ends=ended without a CPU snapshot. An idle
host stop is excluded. Active status snapshots are not atomic. Exact normal-test
counts and independent Palm bytes must both pass; counters alone do not qualify
read timing. No per-transaction hot-path trace or USB operation is introduced.

Native tests cover512 rapid reselection phases,640 short-gap preload/enable
phases,480 sustained-invalid-control cancellation cases,40 selected-rearm/
repeated-address/reset phases, late FIFO replies, stalls and stop clamping.
The emulator models instruction/pin direction arbitration, not pads, input
synchronizers, SRAM/bus contention or owner restart time. CS0/OE endings that are
visible for a PIO clock are latched; finite release latency remains. WE/CS1 are
requalified/polled, not separate every-clock sticky guards. No claim of universal
asynchronous glitch rejection, electrical Hi-Z, numerical setup/hold margin,
physical reset/stop qualification or recognition follows from these tests.
