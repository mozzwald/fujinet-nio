# Experimental bounded ROM response tests

This is a manually armed CS0 responder for the existing Visor Bus Test 0.3
RAM app. It is **not a bootable module ROM emulator**. Full ROM recognition,
insertion, reset, CS1, writes and IRQ service remain disabled or
untested. The default `firmware` build is still passive.

## Build

From this directory, after building/verifying the Palm image:

```sh
./scripts/build.sh host
./scripts/build.sh rom-smoke /absolute/path/visorbus.rom /absolute/path/visorbus.json
```

Output: `build/rom-smoke/springboard_rom_smoke.uf2`. This separate build directory
leaves `build/firmware/springboard_passive.uf2` available for recovery. A missing
ROM/manifest is an error; the experimental target cannot use the passive default
pattern. The manifest/host C model check runs before linking the active target.
Runtime CRC checks the SRAM byte copy before constructing a native 16-bit word
table. Unused addresses return FFFF; the high address lines never wrap the ROM.
Both cores and the ROM table run in SRAM at 150 MHz; PSRAM is unused.

USB debugging uses the same compile-time `SPRINGBOARD_USB_DEBUG=ON/OFF` switch.
An OFF smoke-test image stays disarmed: this experiment deliberately has no
automatic/SWD bypass for USB arming. The eight-word test uses the installed PRC;
the full readback below uses a separate PRC.

## Separate drive-enable scope diagnostic

Build `./scripts/build.sh rom-marker /absolute/path/visorbus.rom /absolute/path/visorbus.json`.
The output is `build/rom-marker/springboard_rom_smoke.uf2`; the normal smoke
build directory is preserved. The banner must include `DRIVE_MARKER=GP39`.
This variant starts disarmed and uses the same bounded arming commands/ROM.

GPIO39 (onboard LED, outside the bus) is a mandatory one-bit PIO side-set
output: high on instructions23..28, low elsewhere. Its transitions accompany
MOV PINDIRS enable/release in the same instruction, without adding PIO cycles.
Core0 LED writes/heartbeat are disabled. The side-set indicates the commanded
PIO drive interval; it does not measure data-pad voltage, pad skew or drive
strength. LED loading and code-layout changes are experimental perturbations.

While stopped, use 10x probes/common module ground: first CH1 CS0/GP16,
CH2 marker/GP39, with the existing falling-CS0 trigger. A subsequent reviewed
capture can use CH1 marker/GP39, CH2 D9/GP29 and a rising-marker trigger.
A late marker supports response latency; an early marker followed by prolonged
intermediate data shifts attention to loading/contention/configuration. Preserve
actual Palm mismatches and do not call a first-word capture a known failing word.

Debug/Release tests run both baseline and marker programs over the same480
cancellation phases, stalls and preload/mapping checks. Pinned EPIO lacks
side-set: the test strips the side-set encoding and explicitly models it each
clock against independently executed data directions. It pre-maps GP39 because
EPIO otherwise drops initialization SET effects before applying the GPIO mux;
silicon retains PIO latches independently. These accommodations are model
limitations, not hardware verification. GP47 remains untouched.

## Full-image readback before module recognition

After the eight-word test passes, install `fujinet-palm`'s separate
`palm/apps/visorcheck/visorcheck.prc` (**Visor ROM Check**, creator FNVC) into RAM.
Keep the RAM Visor Bus Test for now. The ROM itself remains the verified original
7248-byte Visor Bus Test 0.3 image, CRC32 `484e213e`; it does not contain this new
checker. Its expected CRC is generated from the verified artifact when building
the RAM checker, avoiding any checksum embedded in its own checked image.

1. Flash the updated `springboard_rom_smoke.uf2`; it still starts disarmed.
2. In the USB console send `s`, require ROM ready and stopped, then send **`b`**
   and `s`. Require `arm_pending=0 stopped=0 limit=3624`.
3. Open **Visor ROM Check** and tap **Check ROM**, then confirm Read. It reads
   one volatile 16-bit CS0 word per step, with at least one OS tick between steps,
   validates the slot/mapping each time, keeps the screen awake and permits Cancel.
4. Wait roughly 40 seconds. Require **Full ROM CRC PASS**, read bytes `00001C50`
   (7248), and Bus CRC/Expected both `484E213E`. This CRC is calculated from the
   actual bus words on the Prism in big-endian byte order, not a local resource.
5. Send `s`; require `stopped=1 requests=3624 posted=3624 completed=3624
   limit=3624`. `d` retains only the first eight CPU replies. Save photo/status.
6. If interrupted, cancelled or mismatched, send `x`, confirm stopped and retain
   evidence. Leaving the app does not automatically stop the RP. Do not reinsert
   or reset the module. No full ROM registration/launch is enabled by `b`.

Only after this passes, prepare continuous read service and its separate
back-to-back/cancellation checks. Before an eventual launcher test, remove the
**RAM copy of Visor Bus Test** to prevent name/creator shadowing; retain the
separately named **Visor ROM Check**. Keep scope/bench qualifications open.

## First physical test

1. Keep the Springboard seated and Prism awake in the installed RAM app. Do not
   press Pulse/reset or reinsert the module during this test.
2. Flash the **rom_smoke** UF2 using the normal RP2350 BOOTSEL procedure. It starts
   disarmed with data pins as inputs; connecting USB does not arm it.
3. Open its USB serial console. Confirm `EXPERIMENTAL CS0 ROM`, ROM 7248 bytes,
   CRC32 `484e213e`. Send `s`: require `rom_ready=1 ready=1 stopped=1`.
4. Send `a`, then `s`. Require `arm_pending=0 stopped=0` before touching Read.
5. In Visor Bus Test 0.3, select external slot 1 and tap **Read once**, confirming
   the read prompt. The responder stops after eight completed bus cycles.
6. Photograph the eight returned words. Expected offsets 0,2,4,6,8,A,C,E:
   `0000 3000 0000 0000 FEED BEEF 0004 0020`.
7. Send `s`; require `stopped=1 requests=8 posted=8 completed=8`. Then `d` dumps
   the eight CPU reply records. Save these with the photo. `posted=1` means only
   that the CPU submitted data; it does **not** prove the Prism sampled it in time.
8. If counts/words disagree, stop there. Send `x` if needed and confirm stopped;
   retain the evidence. Do not proceed to insertion/ROM launch. A correct
   signature permits planning broader readback; it does not establish ROM launch.

`a` arms eight cycles; `b` arms ROM-size/2 cycles. The LED is solid while stopped
and pulses once per second while armed. `x`
requests stop; if a read is still selected, it waits for PIO-owned release.
`d` is allowed only when stopped with no arm pending. A fresh `a` clears counters
and records. No autonomous repeated retries are performed.

## Output ownership and cancellation

Only PIO1/SM0 owns GP20..35 (D0..D15); all other bus pins remain input-only.
GP39 is the LED; GP47 remains untouched. The 32-instruction APIO program waits for
CS0 release, then a CS0/OE read with CS1/WE high. It pushes one high-window
snapshot. The dedicated, interrupt-disabled core 1 samples the low address and
publishes one SRAM table word. There are no USB calls, allocations or CRC work
in this path. Passive low/high FIFO records are not used as paired addresses.

PIO polls all four control pins and RESET while waiting for TX data. It preloads
the data latch while Hi-Z and rechecks controls/reset before enabling all 16 data
directions. During drive it continues polling. On invalid controls/reset it
releases data directions before setting a sticky IRQ-WAIT completion. A dead CPU
cannot prevent this release. CPU code never enables the data directions.

The same CPU owns reply publication and acknowledgement. While PIO is parked
Hi-Z, it flushes **both** FIFOs before acknowledging IRQ. PIO then requires CS0
release again. A delayed reply from an observed, ended cycle cannot leak into a
new read. Normal operation has at most one RX request and one TX reply; it cannot
fill either four-word FIFO. Blocking request PUSH is performed only while Hi-Z.

## Known timing restriction — no qualification claim

This is a sampled software-defined interface, not a combinational OE gate.
Use this image only for the app's previously captured, widely spaced manual
reads (about 550 microseconds between CS0 windows). Require an invalid/deselect
interval of at least 16 PIO clocks **plus input synchronization** (conservatively
120 ns at 150 MHz). The emulator characterizes the counterexample: a one-clock
deselect can be missed, allowing a stale response. Arbitrary back-to-back ROM
scans are outside this experiment; do not infer support from the signature test.

The 480 phase/condition cancellation tests park within 11 PIO clocks in the current
model, with an asserted ceiling of 12. Data release precedes parking. This does
not include the hardware input synchronizer, pad delay, probe thresholds or
electrical settling. Neither the desired 5–15 ns edge relationships nor the
CPU-to-data deadline have been verified. The 12 MHz logic capture cannot establish
these margins. The current pinned EPIO also lacks silicon IN_COUNT masking on
IN instructions; the program uses an explicit30-pin mask and only bits 0..29 of
its snapshot, so the differing two upper bits are ignored.

Model coverage includes both CS/OE assertion orders, each invalid control/reset
condition at 80 reply phases, CPU stalls/missing and late replies, held selection,
rearm with the next read already selected, data preload/mapping and input-only
ownership. Physical bus response, address/data continuity and full ROM operation
remain pending. Return to bench/scope qualification before enabling boot scans.

## Diagnosing intermittent failed reads

The core0-idle image reduced avoidable timer/GPIO traffic, but a physical run
still had five wrong words. It is not a confirmed fix. The instrumented image
adds a PIO IRQ1 marker immediately after enabling the data output directions.
CPU samples it only at the already-Hi-Z IRQ0 completion, then clears IRQ1 before
acknowledging IRQ0. All 480 cancellation phases also check that this marker
matches whether output drive occurred; missing replies never set it.

USB `s` now emits a second line:

```text
enabled=... not_enabled=... address_errors=... anomalies=...
```

`enabled` counts cycles in which PIO enabled outputs, not successful CPU samples
or adequate data setup. `not_enabled` identifies cycles that ended without drive.
`address_errors` compares the captured address to the sequential word expected
by the installed RAM checker's run. It assumes that test, not arbitrary access.
`e` dumps the first eight anomalous cycle indices, expected/observed addresses,
high snapshots and enabled flags after stopping. Missing CPU request records are
also anomalous. All counters/logs reset on arm.

Flash `springboard_rom_smoke-drive-diagnostics.uf2`, repeat the unchanged RAM
checker0.2/b test, and save both `s` lines and `e` output along with CRC/Details.
No HotSync is required. One extra PIO instruction follows direction enable;
this instrumentation can affect phase/hold timing and does not measure the
exact previous build. The modeled release ceiling remains12PIO clocks, excluding
hardware synchronization and pad delays. Continuous ROM execution remains gated.

For the separate64-word CPU/PIO constant experiment and selectable stage markers,
see [LATENCY_TEST.md](LATENCY_TEST.md). The baseline described above is preserved.
