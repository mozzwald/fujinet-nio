# Live-writable USB ROM drive (experimental hardware candidate)

This separate `springboard_rom_msc` image exposes TinyUSB CDC ACM plus a writable
1 MiB FAT12 disk, label **FUJINET ROM**. Copy a build-generated **SPRINGBD.ROM**
into the root, flush/unmount, then restart the RP2350 at a controlled safe point.
The active SRAM word table never changes during copy, flush, eject, USB bus reset,
or serial commands. No automatic RP/Prism reset or activation is implemented.

The proven 150 MHz ROM-only PIO initializer/instructions, 288-byte ARM responder,
pin configuration and table alignment are preserved. CS1/NIO/USB PacketLink are
not implemented in this variant. CDC is read-only text diagnostics: `status\n`
or `help\n`. Baud/DTR changes do not enter BOOTSEL or reset either device.

## Build and file preparation (engineer-managed)

```
scripts/build-rom-msc.sh /absolute/verified-recovery.rom /absolute/manifest.json /absolute/NEW-build
python3 scripts/rom_msc.py pack /absolute/verified.rom /absolute/SPRINGBD.ROM --version 1 --manifest /absolute/manifest.json
python3 scripts/rom_msc.py inspect /absolute/SPRINGBD.ROM
```

The helper checks an independent Palm ROM manifest before packing. The complete
ROM payload, not a PRC, follows a 32-byte little-endian envelope: magic FNRM,
schema1, nonzero version, even payload length512..65536, IEEE CRC32(payload),
reserved0, reserved0, CRC32(first28header bytes). Each field after magic is u32.
The firmware independently checks the envelope, Palm CRC16/card/store signatures,
HsAT token and bounded database/resource directory/chunk pointers. Original raw
ROM files remain unchanged. Version identifies an image; it is not anti-rollback.

Normal drag/drop or `cp` works. Optional `rom_msc.py copy FILE MOUNTPOINT` checks
that the explicit destination is mounted, writes/fsyncs and compares the copied
file; it never unmounts or restarts hardware. `status PORT` queries an explicitly
selected CDC device and verifies its product identity. No NIO wire IDs changed.

## Storage and durability

Flash offsets: firmware below100000hex, COW staging100000..CFFFFFhex, private
slotA FC0000..FD0FFFhex, slotB FD1000..FE1FFFhex, selection metadataFE2000/FE3000.
Linker assertions and callback bounds protect these regions; the UF2 contains
firmware pages only. Runtime JEDEC capacity must indicate16 MiB. Unknown parts
boot compiled recovery and refuse MSC writes. The prototype USB identity is
CAFE:4013, deliberately distinct from SDK CDC/BOOTSEL; configure an allocated ID
before product distribution. The USB serial comes from cached chip unique ID.

The physical COW store has3072 4KiB records for2048 logical512-byte sectors.
Each record programs two payload pages and then a separate256-byte commit page.
First32 commit bytes: magic FMS1, volume-id46554A49hex, LBA, sequence-low,
sequence-high, payload CRC32, payload-length512, header CRC32(first28bytes).
Remaining commit bytes are FF. Sequences are nonzero64-bit and never wrap.
Scan selects the newest committed record per LBA; only current payloads are
validated, since power loss can tear an erase of an obsolete record. Invalid
committed metadata, duplicate per-LBA sequence or corrupt current payload fails
closed. Writes rotate through noncurrent blocks; current sectors are not erased.
Unwritten LBAs synthesize the initial empty MBR/FAT12 volume. Existing logical
sectors, including filesystem corruption, are never automatically reformatted.

Each write completes durable programming/readback before MSC acknowledges it;
there is no volatile write-back cache. Explicit SYNCHRONIZE CACHE10 handling is
provided. A USB reset during a write can leave an ambiguous acknowledgment; the
sector remains old/new and the host may retry. Sector atomicity does not make a
whole FAT file atomic. Incomplete/corrupt files are rejected at the next import.
A media I/O fault latches the LUN unavailable until RP restart; the SRAM ROM is
not replaced. Wear and real power-cut behavior require hardware qualification.

At startup, before USB enumeration/Core1 service, the bounded read-only parser
checks fixed partition geometry, FAT mirrors, exact root filename, finite cluster
chain and file/package/ROM validity. Fragmented files are supported. The PC is
sole filesystem writer after enumeration; firmware does not mount/update files
while the host owns the disk. Unsupported reformatting/geometry is rejected.

A new valid image is promoted to the inactive private ROM slot, verified, then
selected with dual durable metadata. Identical accepted images cause no rewrite.
Failed import/promotion keeps the previous complete selected image; invalid
private selection uses compiled recovery. The accepted image is copied to SRAM
before the one-shot autoarm. No pending file is accepted merely because it exists.

Linux filesystem `umount` need not issue SCSI eject. `tud_umount_cb` is USB bus
unconfigure, not filesystem unmount. Neither event switches the live ROM. Eject
makes the LUN unavailable until host load/remount, USB mount, or RP restart.

## Keeping bus service independent of flash

All normal functions/data/literals are copied to internal SRAM. Core1 has a
separate4KiB stack and unchanged kernel. Core0 flash callbacks run from SRAM,
mask only Core0 IRQs for each erase/program and intentionally never use multicore
lockout. Buffers must lie in internal SRAM. No external PSRAM/DMA flash reader
is configured. SDK boot-ROM flash operations restore XIP/QMI before IRQ restore.

A RAM vector table alone is insufficient: default SDK exception targets can be
in XIP. The new image supplies SRAM HardFault/NMI handlers and replaces remaining
non-SRAM exception/IRQ targets before writes. It verifies VTOR entries again
after USB initialization. Faults clamp data-pad output overrides and park without
USB, logging or flash access. Both cores share the RAM table; Core1 IRQs remain
disabled. Private-slot writes are additionally rejected after startup.

Static residency/direct-call audits and mapped ARM execution do not prove real
SRAM/QMI arbitration or analog read timing under concurrent programming. Keep
this image experimental until bounded physical read/upload qualification passes.
Do not raise clocks, drive strength or Palm access timing as a shortcut.

## Tests and interpretation

Native Debug/Release CTest includes COW full-disk/wrap/ownership, partial writes,
interrupted erase/program/commit, committed corruption and metadata validation.
`test-rom-msc.py LIB ROM --second-rom ROM --output JSON` runs actual native C,
fragmentation and malformed files, maximum-sized payload, PC mcopy/fsck when
available, and interrupted private-slot promotions/rollback. `test-msc-arm.py`
executes linked callbacks/vector repair/fault clamping with mapped MMIO; physical NOR erase/program and endpoint0 transmission are mocked. `verify-rom-msc.py` independently checks
ELF/BIN/UF2, frozen engine, USB descriptors, SRAM placement and flash boundaries.
`audit-msc-paths.py` records Core1 direct calls and compiler stack usage. Existing
359-scenario persistent ARM/PIO suite and startup guard tests are retained.

Routine copy/readback/status verification requires no logic analyzer or scope.
Physical USB host behavior, real flash persistence and simultaneous Prism read
correctness are separate manual gates. No numerical setup/hold margin is claimed.

## Recovery and rollback

1. To return to a previous uploaded image, copy its archived SPRINGBD.ROM over
   the current file, verify/read back, unmount, then safely restart RP2350.
2. Removing/breaking the file retains the last accepted image; it does not select
   recovery and does not automatically roll back. Previous private slot remains
   protected until the next different valid import uses it.
3. If storage/media is damaged, boot the preserved known-good autoarm UF2 through
   manual BOOTSEL at a bus-safe point. It uses its compiled ROM and ignores these
   storage partitions. Flashing it does not erase staging/private data.
4. Do not erase the entire flash or run repair/format against an unidentified PC
   drive. Filesystem repair requires exclusive PC ownership and explicit action.

An RP restart interrupts ROM availability. Do not restart it while the Prism is
fetching module ROM. First standalone USB tests can be performed with the module
out; a seated live restart requires an independently agreed bus-safe procedure.
Do not require a prolonged manually held Prism reset. All hardware actions are
manual; Codex manages PC collection and guides one action at a time.

ROM activation and PalmOS Setup discovery are separate: a changed ROM may be
active while PalmOS retains a previous Setup database. Do not run CS1 diagnostics
on this ROM-only image or use launcher appearance as the sole upload proof.
