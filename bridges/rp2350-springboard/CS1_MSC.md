# Separate CS1 Stage A + persistent ROM-drive candidate

Build only; no hardware is flashed by these scripts. This candidate retains the
working ROM-drive storage/USB startup and reuses the previously tested asynchronous
CS1 engine (`async_reply.S`, `async_program.c`, `async_stage_a.c`). The installed
ROM-only MSC baseline and its source/artifacts are not changed.

The first target is four bounded PROBE token writes with independently matched
PROBE_ACK reads, plus CS0 FEED/BEEF regression reads between exchanges. This is
Stage A, not a complete packet mailbox or NIO backend. The ROM contains the
existing corrected CS1 Module B Setup; Handspring installs its clone into RAM.
No standalone diagnostic PRC upload is needed.

## Firmware architecture

Core1 owns the single PIO output engine and CS0/CS1 read RX/TX stream. Its300-byte
reply body and50-byte boot-admission gate are byte-identical to p4m-005. The PIO
initializer/template is identical except its known data-address relocation. All
read/write qualification, cancellation, epoch pairing, FIFO overflow quarantine
and pad release rules remain. No clock, pin drive or330ns wait-setting change.

Core0 retires one fully validated write record before/after each TinyUSB task
pass. ACK publication follows validated epoch/outcome; a stale ACK is not command
acceptance. Individual reads never wait for Core0/USB/flash/backend. The full64KiB
CS1 table returns FFFF at reserved offsets. ROM words use a separate64KiB table;
no flash/filesystem reads occur in the live lookup kernel.

The copied file is imported only on RP startup, using the same COW disk/private
slots and CRC/schema/ROM checks as msc-003. Default compiled recovery is the proven
Phase2 ROM. A previously stored Phase2 file will still select Phase2; the provided
new SPRINGBD.ROM must be copied/verified before expecting Module B installation.
An invalid/missing intended file blocks the host ready gate rather than running
the wrong diagnostic. Existing private slots and ROM-only fallback are preserved.

USB has a distinct laboratory identity cafe:4014, product **FujiNet CS1 ROM Drive
A1**, CDC+MSC. The disk remains FUJINET ROM. `cs1_msc.py` validates the device
before sending bytes, uses an exclusive CDC handle, validates bundle hashes/build/
ROM/clock and preserves raw replies. Do not use old q/s/x/b console tools here.
No serial baud/DTR/eject action reboots a device or activates an image.

## Explicit control

`status` is read-only. Live read totals have `counters_final=0`; do not interpret
them as final totals or stop ROM service just to count reads. Core0 accepted/ACK
and sticky capture/error status are independently available.

`recover` is an explicit, quiescent-client session restart. It requires verified
ROM, a ready owner and current inactive strobes/reset released. Core0 clamps and
requests stop; only after Core1 park acknowledgment does it reset write state/
table and request a new arm. No automatic reset/fault recovery or replay occurs.
It briefly interrupts ROM availability and invalidates old pending transactions.
Host command requires `--client-idle`; use only after a manual RAM-app-idle cue.
A failed recovery is logged and not retried automatically. Do not run during
installation, an active command, or arbitrary ROM execution.

The RAM diagnostic uses only MOVE.W, computes CS1=API CsBase+CsSize, verifies RAM
PC/code CRC/heap and module generation/setup guards, then issues tokens1..4 on a
fresh session. It waits for the matching ACK before another write, with bounded
polling and specific Guard diagnostics. Full128-byte PDB records and the distinct
B install receipt are decoded/joined automatically. Old A records remain intact.

## Build and tools

```
scripts/build-cs1-msc.sh /absolute/recovery.rom /absolute/recovery-manifest.json /absolute/NEW-build
python3 scripts/rom_msc.py pack /absolute/cs1-module-b.rom /absolute/SPRINGBD.ROM --version 2 --manifest /absolute/rom-manifest.json
```

Current Stage A is selected in a separate target; existing MSC and old Stage A
build defaults remain separate. Build ID tracks reviewed source inputs and ROM;
CMake reconfigures if those inputs change. The complete UF2/source/config hashes
are preserved alongside ELF/BIN verification.

Engineer's `cs1_msc.py status|ready|recover --port ... --bundle ... --output NEW`
never flashes or resets. Recover additionally needs `--client-idle`.
`fujinet-palm/tools/cs1_msc_collect.py` reads one MCU status, then retrieves both
B PDBs with serial palm-sync. It preserves failures and declares functional PASS
only when matching install/runtime/CRC/RAM evidence and four accepted MCU ACKs
agree. No scope/analyzer code runs. No persistent console polling during reads.

## Qualification boundaries

The user explicitly deferred concurrent flash-writing/ROM-read tests and removed
the scope/analyzer. The first physical run uses software/data evidence only.
Keep SPRINGBD.ROM/disk unmounted while performing the CS1 test so the workload
stays controlled. The drive remains writable; concurrent flash/CS1 hardware
qualification is not claimed. Finite FIFO/one-outstanding-write constraints and
explicit overflow faults remain; no write-loss safety test is relaxed.

Native/compiled tests cover control classification, alternating selects, back-to-
back reads, delayed Core0 retirement, cancellation, overflow, stale owners, reset,
output release and explicit recovery. They do not simulate physical dual-core
SRAM/QMI arbitration or establish a measured setup/hold/Hi-Z margin. Retain those
unknowns; use probes later only if a concrete problem requires them.

Installation discovery is separate from selecting a flash slot. Use the proven
MSC ROM-only responder to install/verify B first, before changing bus engines;
then hold the ROM image constant for the CS1 firmware trial. Do not rely on a
soft reset to refresh a cached Setup, repeatedly flash/reset, delete OS-owned
Setup databases, or hold Prism reset during PC preparation. All physical actions
remain manually cued; preserve rollback and collect evidence before retries.
