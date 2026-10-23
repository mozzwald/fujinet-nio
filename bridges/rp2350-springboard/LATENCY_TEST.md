# Isolated ROM latency and constant-response diagnostic

Build `./scripts/build.sh rom-latency /absolute/visorbus.rom /absolute/visorbus.json`.
Output: `build/rom-latency/springboard_rom_latency.{uf2,elf}`. This does not
replace the baseline smoke/marker build directories. USB console is required;
UART GPIO assignments are not added. It starts disarmed, runs from SRAM at
150 MHz, and uses PIO1/SM0/GPIOBASE16/divider1. GP47 remains untouched.

Use the matching RAM checker0.5.0 and updated `fujinet-palm/tools/prism_collect.py`.
Install only its PRC through serial palm-sync when updating an existing session;
retain the original control PDB, session token, sequence and all saved records.
Do not run setup/install on an established session or regenerate its control.
The new tests read exactly64 words at offsets0..126 with the existing guarded,
one-tick-spaced reader and unchanged Palm timing. They do not launch any ROM.

## Response and marker selection

Configuration commands are accepted only while stopped with no arm pending.
The collector selects them and requires exact `q` readback before arming.

| Command | Response | CPU in response data path |
| --- | --- | --- |
| R | cpu-rom, original ROM prefix | RX/SIO address/table lookup/TX |
| Z | cpu-zero, SRAM table filled0000 before arm | identical CPU lookup path |
| F | cpu-ones, SRAM table filledFFFF before arm | identical CPU lookup path |
| z | pio-zero | no CPU TX reply |
| f | pio-ones | no CPU TX reply |

| Command | GPIO39 high interval | Observable stage |
| --- | --- | --- |
| 0 | PCs23..28 | direction enable through release |
| 1 | PCs7..15 | request PUSH to reply PULL |
| 2 | PCs16..22 | PULL to direction enable |
| 3 | PCs17..22 | output-latch preload to direction enable |

Mandatory side-set changes instruction bits only; no PIO instructions or delay
cycles are added by selecting a marker. The PIO constant modes replace only
instruction13 (STATUS becomes X=0) and16 (PULL becomes OSR=0/~0). All other
encoded instructions, requalification, preload and release remain identical to
the CPU program after stripping side-set. All data latches load at17; directions
enable at23, five PIO clocks later (logical model, not data-pad timing).

`b` arms64 reads. `x` stops, `s` reports status, `q` reports selected response,
marker and budget. `d` CPU reply traces apply only to CPU modes. In PIO modes,
`posted=0` means no CPU TX writes; requests/completed/enabled must each equal64.
PIO pushes one snapshot then serves its constant autonomously. CPU drains the
snapshot at the released IRQ0 park and acknowledges before a new cycle. The
CPU remains responsible for counting/arming/acknowledgement, not generating the
constant or releasing an active response. No sampled low-address trace is
claimed for PIO mode, since a stalled CPU might resume after deselection.

## Collection examples

From `fujinet-palm`, using an existing verified session:

```sh
./tools/prism_collect.py run /path/to/session --kind rom-prefix --responder cpu
./tools/prism_collect.py run /path/to/session --kind constant-zero --responder cpu
./tools/prism_collect.py run /path/to/session --kind constant-zero --responder pio
./tools/prism_collect.py run /path/to/session --kind constant-ones --responder cpu
./tools/prism_collect.py run /path/to/session --kind constant-ones --responder pio
# Fixed response, change only the marker profile on each separate capture:
./tools/prism_collect.py run /path/to/session --kind rom-prefix --marker request
./tools/prism_collect.py run /path/to/session --kind rom-prefix --marker reply
./tools/prism_collect.py run /path/to/session --kind rom-prefix --marker preload
```

These are individual, manually cued runs. Arm the reviewed scope capture before
each collector, and tap only its matching ROM64/Zero/Ones button after the host
cue. Wait for sound/save, then serial HotSync. Never run the block unattended.
A PASS validates only that pattern readback; it does not qualify ROM operation.

## Stopped-state probe pairs and limitations

Use both probes10x/common module ground,100ns/div/500MS/s/4kpt to start. Move
probes only after confirming RP/scope stopped. Pair CH1CS0/GP16 with CH2marker/
GP39, fallingCH1 trigger, to measure selected stage boundaries from CS0 in the
same frame. Pair marker and a data bit for preload/enable-versus-pad behavior;
choose trigger edge for that marker's meaning. Keep the physical pair unchanged
through a CPU/PIO comparison. The Hantek SINGLE sweep can arm acquisition;
verify actual ON/SINGle/NOTRIG before the read cue. Do not force-trigger or use
unvalidated raw CH2 decoding; preserve raw packets and tracking-cursor exports.

GPIO39 is a command/stage marker, not valid data or drive current. Output-pad
skew, probe response and the Prism sampling point remain unqualified. Request
marker includes RX publication, CPU polling/service, TX publication and PIO's
STATUS polling; it is not a measurement of the table load alone. Its falling
edge is PULL, not the exact CPU FIFO-write time. Preload/enable markers isolate
the fixed PIO tail. Separate optional CPU instrumentation would perturb that
path and must be measured/labeled separately; do not infer sub-stage timings
by subtracting unrelated waveform acquisitions.

## Cancellation, release and stop

Normal release remains PIO-polled: CS/OE/WE/CS1 sampled together, RESET checked
on a separate JMP PIN. CPU stalls cannot block release once data is driven.
Missing replies and RX-full stalls stay Hi-Z. Release precedes IRQ0 parking;
CPU flushes both FIFOs before acknowledging. Constants never pull queued TX
words. Guard tests cover all modes/profiles and delayed/queued replies.

This is **not** a combinational CS/OE output gate. A one-PIO-clock deselect can
be missed in every mode/profile; rapid reads may retain stale drive. The existing
widely spaced Palm stimulus remains the only permitted active experiment. The
47ns observed release tail is compatible with polling/synchronization latency,
not proof of a safe turnaround interval. No rapid-cycle or boot qualification.

Constant-mode explicit abort cannot use the CPU responder's empty-RX idle test.
It first forces all16 data-pad output enables low, then disables SM, injects
MOV PINDIRS,NULL, and returns pins to SIO input. A CPU stall during the override
loop leaves PIO running to release on controls; once SM is disabled all data
OEs are already inhibited. Retained PIO directions are cleared before rearm.
This exceptional abort path is outside normal measured cycles and requires
physical validation; aborted runs cannot be reported as a complete PASS.

Pad settings deliberately match the original program: APIO input-only setup
writes the whole pad register, leaving data drive strength field0 (2mA), slow
slew and Schmitt disabled; data mux then clears output-disable. GP39 retains its
separate LED-pad setup. No stronger drive, pull resistor, synchronization bypass,
clock change or Palm wait-state change is part of this experiment.

## RX-first response implementation

`./scripts/build.sh rom-fast /absolute/visorbus.rom /absolute/visorbus.json`
builds a separate candidate at `build/rom-fast/springboard_rom_latency.{uf2,elf}`.
`rom-latency` retains the original CPU engine; its archived binary remains the
comparison baseline. The candidate announces `ENGINE=rx-first-v2 full_words=N` separately
from its compatible diagnostic/ROM identity. Verify this line after flashing,
before using the existing collector; that collector does not enforce the engine
identity. No Palm reinstall or control/session rewrite is needed.

The candidate separates CPU request wait, reply publication, and released-park
accounting. The request loop reads RX status directly and checks the idle stop
flag. It does not poll the completion IRQ before RX or reconstruct addresses,
increment counters, or save traces in the response path. The small ARM routine
prepares its saved registers/constants before waiting, then reads the request,
samples the low address, checks high address bits, indexes the native SRAM table
and publishes TX. Result stores follow publication. Completion accounting,
sequential-address validation and traces happen at the released IRQ park.

The PIO program and all response modes, pads, clock and bus wait settings remain
unchanged. An accepted request always PUSHes before it can cancel/park. If CPU
wakes after cancellation, RX and IRQ can both be pending: the CPU can publish a
late word, but PIO remains Hi-Z and parked. That same CPU flushes FIFOs before
acknowledging IRQ0, preventing the late word from reaching the next request.
No early IRQ acknowledgement, output-enable bypass or rapid-cycle guarantee is
introduced. Idle stop before consuming RX cannot enable CPU-mode outputs without
a TX reply. Constant-mode stop retains its pad-OE inhibition path.

Run `bash tests/test_cpu_reply_arm.sh` with `arm-none-eabi-gcc` and `qemu-arm`.
It executes the actual assembled kernel against RAM register substitutes:
100385 cases cover all32768 table indices, every upper address bit, all32
RESET/control combinations, random snapshots and idle stop. This is functional
instruction/ABI validation, not M33 or peripheral timing emulation. The existing
PIO model includes1536 RX-first cancelled/late-publication cases in addition to
9600 cancellation phases; short deselects remain a known failing qualification.

The first physical candidate test should read the real ROM prefix with the
existing ROM64 checker/collector and preserve the GPIO39/OE pair and scope
selector. Require the engine/ROM identity and disarmed state first. Preserve
actual trigger or no-trigger status; after no-trigger SINGLE, this scope may
require cancelling the sweep toAUTO before STOP takes effect. Never download
the old trace or force a trigger. A clean prefix is necessary before a wider ROM
test and does not establish full-ROM, timing, release or boot qualification.

PIO can generate constants and control the pins autonomously. It has shift/
scratch registers and FIFOs, without an instruction to index the system SRAM
ROM table. A CPU-free arbitrary-ROM path would additionally need DMA lookup and
cross-window address/request/cancellation coordination; constants alone do not
implement that mechanism. Reference: RP2350 datasheet §§11.1,11.2.2–11.2.4.

### Full image after the prefix check

Engine v1 was limited to `b`/64 reads. Engine v2 adds explicit `B`/full-image
arming, permitted only with `cpu-rom` selected and a stopped/ready responder.
Its banner advertises the exact image word count,3624 for the current7248-byte
ROM. `b` and every response/PIO instruction remain unchanged; the ARM reply
routine is unchanged. Budget extension does not qualify timing or ROM execution.

Use the updated collector with an explicit opt-in:

```sh
python3 tools/prism_collect.py run /path/to/session --kind rom --latency-full-rom
```

This verifies the engine-v2 capability against the session ROM, selects CPU-ROM,
sends `B`, and verifies the new run and exact armed budget before cueing **Run**,
then **Read**. It rejects enginev1, short budgets and PIO/pattern modes without
falling back to another arm method. The existing Palm Run reads the entire ROM
with the established guarded, one-tick-spaced reader; no PRC or control change.
The default collector command retains the legacy firmware identity and `b`
method. Keep the explicit flag for this firmware; never send a full Palm Run
to a64-word diagnostic arm. Retain pending/full-engine metadata through retries.
