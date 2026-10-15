# RP2350 Springboard base firmware

Independent Core2350B (B1/B2) bring-up project for a Handspring Visor Prism.
The root NIO firmware build does not include this directory. This is a **passive
capture image**, not yet an active ROM emulator or a USB/NIO bridge. It cannot
make the Prism recognize a Springboard module. Do not connect it to a Prism
until the private bring-up plan's electrical/physical gates have passed.

## Implemented

- 150 MHz RP2350 ARM build, copied to SRAM at boot, dedicated capture core.
- Input-only GPIO initialization with no UART/USB console and no data/IRQ drive.
  GP39 (LED) and GP47 (B1/B2 PSRAM CS) are excluded. PSRAM is unused.
- Four passive PIO observers: low GPIO0–31 window on PIO0 and high GPIO16–47
  window on PIO1; SM0 captures /CS0 and SM1 captures /CS1 in each block.
- Explicit release-before-assert guard, one sample at CS assertion, bounded
  SWD-visible trace, FIFO stall and software overflow reporting.
- SRAM ROM copy with CRC32 verification, byte-address reconstruction, big-endian
  word lookup with explicit out-of-range fill, and local ID/ABI/scratch/counter
  register model. Register operations are host-tested, not connected to the bus.
- Debug/Release tests execute the same apio capture code under epio. Tests check
  both GPIO windows, control guards, full raw samples, saturation and Hi-Z.

Capture samples at initial CS assertion are **not write-data samples**: write
data may arrive later. The windows are independent streams, not coherent paired
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

## First-board procedure (only after physical gate approval)

1. Program the UF2 detached from the Prism via BOOTSEL or a verified SWD setup.
   Confirm the file hash and inspect idle pin directions. No command here flashes.
2. Through SWD, check `sb_debug_rom_ready == 1`. `sb_rom_sram` must match the
   image. The PRC can be installed independently into Prism RAM beforehand.
3. Set `sb_debug_stop = 1` and wait for `sb_debug_stopped == 1` before reading
   `sb_debug_trace` and `sb_debug_fifo_stalls`. Use a reboot to rearm for a new run.
   Do not infer atomic trace snapshots while the producer is running.
4. For insertion capture, arm before insertion. The 128 entries hold raw samples
   from four streams; expect the buffer to fill quickly. `count` never exceeds
   128; `dropped` and sticky per-PIO RXSTALL bits must be recorded. A loss-free
   short trace only establishes that observed subset, not sustained throughput.
5. Compare raw records with independent analyzer traces. Live
   `sb_debug_gpio_low/high` are SIO GPIO0–31/32–47 samples, not paired PIO records.
6. Save hashes, clock, wiring, instrument settings, raw traces and results in the
   private plan repository. USB-powered GPIO can back-power an unpowered peer;
   firmware Hi-Z is not proof of powered-off electrical isolation.

No USB transport, IRQ generation, active read response, mailbox FIFO or network
service is included. These are subsequent phase deliverables.
