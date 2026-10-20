# RP2350 Springboard base firmware

Independent Core2350B (B1/B2) bring-up project for a Handspring Visor Prism.
The root NIO firmware build does not include this directory. This is a **passive
capture image**, not yet an active ROM emulator or a USB/NIO bridge. It cannot
serve module ROM data; OS slot registration alone does not prove ROM response. Do not connect it to a Prism
until the private bring-up plan's electrical/physical gates have passed.

## Implemented

- 150 MHz RP2350 ARM build, copied to SRAM at boot, dedicated capture core.
- Input-only bus GPIO initialization with no UART console and no data/IRQ drive.
  Optional core-0 USB CDC debug console, enabled by default for bring-up.
  GP39 drives only the onboard LED; GP47 (B1/B2 PSRAM CS) is untouched.
  PSRAM is unused.
- Four passive PIO observers: low GPIO0–31 window on PIO0 and high GPIO16–47
  window on PIO1; SM0 captures /CS0 and SM1 captures /CS1 in each block.
- Starts stopped; USB/SWD arm clears prior trace, FIFOs, sticky stalls and
  per-stream sequence counters. Stops automatically at 128 raw records.
- Release-before-select guard, one read-qualified sample per CS selection,
  bounded SWD-visible trace and FIFO stall reporting. A retained sample itself
  must show selected CS, the other CS high, OE low and WE high.
- SRAM ROM copy with CRC32 verification, byte-address reconstruction, big-endian
  word lookup with explicit out-of-range fill, and local ID/ABI/scratch/counter
  register model. Register operations are host-tested, not connected to the bus.
- Debug/Release tests execute the same apio capture code under epio. Tests check
  both GPIO windows, control guards, full raw samples, saturation and Hi-Z.

The observer polls snapshots until CS/OE indicate a read, accepting either
assertion order. It rejects writes, abandoned selects and both-selects-low states.
This is **not late-cycle read-data validation**: even with OE low, data setup may
not have completed. Short pulses may be missed. No write-data capture is provided. The windows are independent streams, not coherent paired
addresses. Sequence numbers are per stream and do not prove cross-window pairing.
Any FIFO RXSTALL or software drop invalidates completeness. Hardware input
synchronization and physical timing are not proven by epio tests.

BENCH_ACTIVE/PRISM_ROM names are reserved in the model and explicitly rejected.
Active output qualification, late-response cancellation, write sampling and
cross-window transaction correlation remain future work after passive evidence.
There is no claim of 300 ns capability or a completed Phase 1 gate.

## Build

Prerequisites: Python 3, Git, CMake ≥3.21, native C compiler; firmware also needs
`arm-none-eabi-gcc/g++` and newlib. Dependency bootstrap/validation and picotool
integration are reused from `rp2350-zorro`; exact pins are in `dependencies.json`.
Dependency edits require deliberate manifest review; dirty checkouts are rejected.

```sh
./scripts/create-deps.sh
./scripts/build.sh host
./scripts/build.sh firmware
```

The last command embeds a small **non-ROM test pattern**. To embed the actual
Palm test ROM, first build and verify it in sibling `fujinet-palm` as described
in `palm/apps/visorbus/README.md`, then from this directory:

```sh
./scripts/build.sh firmware "$(realpath ../../../fujinet-palm/palm/apps/visorbus/build/visorbus.rom)"
```

Outputs: `build/firmware/springboard_passive.{elf,uf2,bin}` and its ELF map file.
Embedding a ROM does not enable bus outputs. Inspect the map to confirm SRAM
placement of `capture_core`, `sb_capture_program_init`, `sb_trace_push` and
`sb_rom_sram`. The full image runs from SRAM; no overclock or flash write occurs.

### USB debug build switch

Firmware builds enable the experimental USB console by default. To disable all
USB debug code and USB stdio linkage while keeping the LED/SWD capture support:

```sh
SPRINGBOARD_USB_DEBUG=OFF ./scripts/build.sh firmware /absolute/path/to/visorbus.rom
```

Use `SPRINGBOARD_USB_DEBUG=ON` to re-enable it. The build script explicitly sets
the option each time (default ON), so a prior OFF CMake cache cannot silently
disable a normal bring-up build. Direct CMake builds can use
`-DSPRINGBOARD_USB_DEBUG=OFF` or `ON` instead. Host-model tests do not initialize USB.

### USB CDC console

On Linux, open the new `/dev/ttyACM*` device with a terminal such as minicom at
115200 baud, hardware flow control off, and DTR asserted. CDC baud is a host-side
setting, not a physical UART rate. About 250 ms after connection, the firmware
prints its identity, clock, embedded ROM size/expected CRC and current readiness.
Reconnecting repeats the banner. A status line follows every five seconds.

Single-character commands (no Enter required, no local echo):

| Key | Action |
| --- | --- |
| `h` | Help |
| `a` | While stopped: request clear/arm; cancels any dump. Wait for a new run number, `arm_pending=0` and `stopped=0` before stimulus |
| `s` | Readiness, stopped state, FIFO stalls/live GPIO, run number, arm pending, capacity limit and queued words at stop |
| `r` | GP45 RESET level and sampled asserted/released edge counters |
| `x` | Request capture stop; wait for `stopped=1` |
| `d` | Dump the stopped trace and drop count; refused while capture is live |

USB debug v3 retains `r` and adds arm/rearm for Visor Bus Test 0.3. Core 0
samples GP45; the first sample establishes a baseline without counting an edge.
`initialized=1` means a baseline exists, not a sample count.
Level 0 is asserted, 1 is released. Read `r` before and after a pulse: a complete
observed pulse increments both counters once and ends released. Sampling continues
after capture stop. Short pulses can be missed; this is not timing instrumentation
or proof of any address/data exchange. No bus pin is driven by the monitor.

Capture starts **stopped**, including USB-disabled builds. SWD can set
`sb_debug_arm=true` after readiness/stopped acknowledgement. Core 1 alone owns
capture initialization, trace clearing and PIO reset; core 0 requests arm/stop.
`a` is refused while live or pending; `x`/`d` are refused during pending arm.
`d` is refused while live. Wait for `stopped=1` before dumping. Trace readout
never races a live producer, and another `a` begins a new numbered run without
rebooting or touching module RESET. RESET counters remain cumulative across runs.

At 128 records capture freezes with `limit=1`: this is a **bounded prefix**, not
proof a whole transaction sequence was retained. `dropped` counts attempted
software appends beyond capacity (normally zero because capture now stops).
`queued` reports raw FIFO words still queued at stop, per PIO block; these are
excluded from the retained prefix, not a complete count of omitted cycles.
Any FIFO stall/drop invalidates completeness. Even with zero stalls/drops,
`limit=1`, queued words, polling or the independent windows prevent claims of a
complete paired transaction trace. Per-stream sequence numbers are not timestamps.
Trace records are emitted at most one per 10 ms console pass. Live GPIO/stall
fields are separate samples, not atomic snapshots.

### Inserted-module bounded read observation

1. Install Visor Bus Test 0.3 in Prism RAM. Confirm slot 1 is removable, hardware
   and software installed, RESET released, and all mapping queries succeed.
2. Keep the module seated and Prism awake. In USB debug v3 enter `a`, then `s`.
   Proceed only after the run number increments, `arm_pending=0`, `stopped=0`,
   `limit=0` and FIFO stalls are zero. If capture already filled, dump it and
   investigate background traffic before attributing it to the test app.
3. Tap **Read**, confirm the eight-word CS0 probe once, and record its screen.
   The app rechecks state/mapping after confirmation and before every word read.
4. Enter `x`, wait for `stopped=1`, then `s` and `d`. Save both the stop metadata
   and every record through `trace end`. `a` can repeat without reinsertion.
5. No expected ROM payload exists yet: the RP never drives data. Compare observed
   address/control activity with the eight offset0..0x0E reads, not ROM contents.
   Do not use Pulse on a software-registered slot. This is observation, not a
   timing or active-emulation qualification.

USB initializes on core 0 after capture launch, with no wait for a terminal.
Messages use a fixed 192-byte stack buffer, zero USB-output-space/mutex wait
timeouts and no explicit output flush. Input consumes at most one byte per 10 ms
pass. Output is best effort: disconnects or a host that stops reading may lose or
truncate lines; reconnect or repeat `s`/`d`. USB baud/vendor reset shortcuts are
disabled; use physical BOOTSEL/RUN or SWD to reflash/reset.

USB SDK background work and interrupts also run on core 0. There are no USB calls
or prints in the capture core, but shared SRAM/interconnect load still needs
hardware comparison with USB disabled. This console is for passive bring-up, not
proof of active bus timing. Future ESP32-hosted NIO USB transport must replace or
explicitly integrate it; the SDK stdio console owns the USB device.

## First-board procedure (only after physical gate approval)

1. Program the UF2 detached from the Prism via BOOTSEL or a verified SWD setup.
   Confirm the file hash and inspect idle pin directions. No command here flashes.
2. Through SWD, check `sb_debug_rom_ready == 1`. `sb_rom_sram` must match the
   image. The PRC can be installed independently into Prism RAM beforehand.
3. Capture starts stopped. Set `sb_debug_arm = 1` after readiness; wait for a
   new run and arm acknowledgement. Set `sb_debug_stop = 1` and wait for stopped
   before reading `sb_debug_trace` and `sb_debug_fifo_stalls`. Rearm without reboot.
   Do not infer atomic trace snapshots while the producer is running.
4. For explicitly authorized insertion capture, arm before insertion and reject
   any nonempty/dirty baseline. The 128 entries hold raw samples from four streams;
   capture freezes at capacity. Record limit, queued words, dropped and RXSTALL.
   A short trace establishes only the observed subset, not sustained throughput.
5. Compare raw records with independent analyzer traces. Live
   `sb_debug_gpio_low/high` are SIO GPIO0–31/32–47 samples, not paired PIO records.
6. Save hashes, clock, wiring, instrument settings, raw traces and results in the
   private plan repository. USB-powered GPIO can back-power an unpowered peer;
   firmware Hi-Z is not proof of powered-off electrical isolation.

## Onboard status LED (GP39)

The Core2350B LED lights when `main()` initializes the bus inputs, before the
clock change and ROM copy. Once capture is armed after ROM CRC verification and initialization,
it pulses for 100 ms once per second. A ROM-copy/CRC failure
blinks 100 ms on / 100 ms off. While capture is waiting for arm, initializing,
or stopped (including capacity stop), it stays on. LED updates run on core 0 without sleeps;
core 1 never updates the LED.

The heartbeat shows the main loop is running and capture initialization completed.
It does not prove bus activity, loss-free capture or Visor communication. GP39
also appears in raw high-window samples; address reconstruction excludes it.
Check `sb_debug_capture_ready`, ROM readiness, FIFO stalls and trace drops through
SWD for those separate states. LED polarity follows the active-high circuit in the
[Waveshare schematic](https://files.waveshare.com/wiki/Core2350B0/Core2350B.pdf).

No NIO USB transport, IRQ generation, active read response, mailbox FIFO or network
service is included. These are subsequent phase deliverables.
