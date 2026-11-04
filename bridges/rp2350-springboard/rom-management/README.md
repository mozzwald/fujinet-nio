# Persistent USB Springboard ROM storage — experimental ROM-only candidate

This candidate removes firmware rebuilds for subsequent ROM changes. It preserves
`pio_rearm_program.c`, `persistent_reply.S`,150MHz, pin/drive configuration and the
65536-byte SRAM word table's proven modulo16=8 bank alignment. It deliberately
uses the proven ROM-only engine, not the unqualified CS1 experiment. No NIO or
PacketLink implementation is included. Hardware programming/persistence and
Prism readback remain unqualified until a manually authorized bench test.

## Audit and partitions

Core2350B's SDK board file declares16MiB flash. The candidate checks JEDEC command
9F before USB/Core1 startup and accepts a nonzero/non-FF manufacturer plus capacity
byte18hex (16MiB). Unknown capacity refuses flash management, skips high-flash slot
reads and boots compiled recovery. This is a runtime check, not a physical flash
measurement already performed on this candidate.

All offsets below are relative to flash XIP base10000000hex. The linker asserts
16MiB FLASH and a firmware LOAD end at or below10100000hex. Separate symbols bind
the slots/metadata to the following layout. Host requests cannot provide a raw
flash erase/program address. Both portable storage and hardware callbacks bound
all writes. The UF2 contains firmware pages only, never slot/metadata pages.

| Region | Offset | Size | Purpose |
|---|---|---|---|
| Firmware + compiled recovery |000000–0FFFFF|1MiB cap|Linker-enforced load boundary|
| Unused |100000–FBFFFF|Reserved|No management writes|
| Slot0 header |FC0000|4KiB|256-byte commit header|
| Slot0 payload |FC1000|64KiB|Exact uploaded ROM bytes|
| Slot1 header |FD1000|4KiB|256-byte commit header|
| Slot1 payload |FD2000|64KiB|Exact uploaded ROM bytes|
| Metadata0 |FE2000|4KiB|256-byte active-selection record|
| Metadata1 |FE3000|4KiB|Alternate active-selection record|
| Unused |FE4000–FFFFFF|Reserved|No management writes|

Only even512..65536-byte images and nonzero32-bit image versions are accepted.
Version is transport image identity, not Palm database version or anti-rollback.
Firmware schema version is1. Palm byte order is preserved in flash; the original
ROM-to-native-halfword conversion fills the SRAM table before service enables.
The host additionally checks the FEEDBEEF card signature at offset8; transport
CRC does not replace the existing independent Palm ROM semantic/header verifier.

Both header formats are8 little-endian32-bit words followed by FF padding to256.
CRC32 is standard reflected IEEE/zlib over the first28 bytes. All padding must be
FF. Image header: magic31494D52, schema1, slot0/1, image-version, length, payload
CRC32, reserved0, header-CRC. Metadata: magic31414D52, schema1, generation, selected
slot0/1/2, image-version, length, payload-CRC, header-CRC. Slot2 denotes compiled
recovery and requires zero image-version/length/CRC in metadata. Generations do
not wrap: UINT32_MAX refuses another activation.

## Durability and ownership

BEGIN protects the selected slot even if its data is invalid. It erases only the
inactive slot, header first, then accepts strictly sequential pages owned by a
nonzero volatile upload nonce. All pages are read back. FINISH checks the entire
payload CRC before publishing its valid header LAST. Reset during upload loses
the volatile nonce; no continuation or activation occurs automatically. An image
can be valid in an inactive slot without being selected or served.

ACTIVATE separately revalidates the image and writes a new higher-generation
record into the other metadata sector, preserving the current record until the
new commit completes. Valid metadata binds slot, version, length and CRC. A
power cut selects an old or new complete record, never a partial publication.
A bad selected payload falls back to compiled recovery, not another arbitrary
slot. No/invalid metadata also uses recovery. Equal conflicting valid generations
fail closed to recovery and reject mutation rather than guess ownership.

ROLLBACK uses the previous metadata record and verifies its exact binding. If
that old inactive image has since been overwritten, rollback returns CONFLICT;
it does not silently substitute the newly uploaded bytes. With no previous record,
rollback selects recovery. RECOVERY explicitly persists source2. A transport I/O
failure is ambiguous: storage writes and RESUME latch blocked until a fresh explicit
QUIESCE/rescan, which reloads the validated persistent selection into SRAM. A lost activation reply can mean the new record committed. Inspect
state; never automatically resend a mutating request.

## Flash/XIP and physical maintenance gate

Normal bus reads use SRAM only. Upload, payload verification/readback, activation,
rollback and recovery require explicit maintenance. The candidate does not assume
an idle Launcher, powered-off screen or a single sampled inactive select is safe.

The engineer may issue QUIESCE only while **the Prism CPU is held in physical
hardware reset, module /RESET GPIO45 is low, and CS0/CS1 are high**. An
HsCardAttrReset setting affecting only the module is insufficient: it does not
prove that the Prism CPU stopped making bus accesses. Unselected OE/WE activity
is not a module transaction and does not poison this gate (as seen in earlier
bench captures). QUIESCE requires a1ms quiet interval, clamps all
data outputs, requests/awaits Core1 stop, parks PIO, then hardware-resets Core1
into its boot-ROM wait. It never depends on a lockout interrupt serviced by the
interrupt-disabled response kernel. Core0 executes from SRAM, disables interrupts
around each SDK flash operation, and uses SRAM page buffers. No DMA is used.
GPIO raw edge flags latch CS0/CS1 assertion or RESET release even while interrupts
are disabled. Gate checks run before/after every erase/program and while idle in
maintenance. A violation blocks writes and leaves outputs clamped; no automatic
resume or recovery occurs. A completed flash operation cannot be undone, so a
violation/I/O error requires explicit state rescan before proceeding.

This gate requires the operator's external reset hold. It is not proof against an
independent bus master ignoring RESET. Actual RP flash/USB/reset isolation behavior
still needs controlled physical qualification. If the operator cannot maintain
that isolation, uploads must be refused; no live-service upload bypass exists.
Do not hold reset while engineers spend minutes building/preparing tools. Prepare
first; the physical hold is only for the bounded management session. Duration has
not been physically measured or promised.

Activation copies the validated image into both byte buffer and SRAM word table
with service stopped. It never enables service. Explicit RESUME launches the
unchanged Core1 boot path; it waits for reset/select qualification before enabling
PIO. `active=1` is distinct from metadata selection and SRAM preparation. Stop/fault
never automatically triggers an arm retry. GPIO/PIO cancellation/Hi-Z logic is
preserved. No setup/hold margin is claimed from simulation or a status response.

## Dedicated USB protocol and PC utility

CDC product is **FujiNet ROM Manager v1**,VID2E8A/PID0009. The Linux utility checks
sysfs identity BEFORE opening/sending data. It refuses the old debug console and
future NIO interfaces. This CDC parses RMU1 frames only: no x/b/R character
commands, reboot escape, NIO PacketLink or binary/text auto-detection. Future NIO
must use another interface or separately negotiated firmware transport.

Header16 bytes: ASCII RMU1; version1:u8; opcode:u8; payload-length:u16le;
request-id:u32le; CRC32:u32le. CRC covers header bytes0..11 plus payload. Maximum
payload512. Partial frames expire after1s. Response opcode=request-opcode|80hex,
same request-id, payload begins status:u32le (0 success). Requests are never
replayed automatically. CDC output allows a bounded2s frame-write timeout on
Core0; Core1 bus service never waits on it. This is not UART logging per access.

| Opcode | Request payload |
|---|---|
|1 STATUS|empty|
|2 QUIESCE|empty|
|3 BEGIN|slot,version,length,CRC32,nonce: five u32le|
|4 CHUNK|nonce,offset: u32le +1..256 exact sequential bytes|
|5 FINISH|nonce:u32le|
|6 READ|slot,offset,length: u32le, max256 bytes|
|7 ACTIVATE|slot:u32le|
|8 ROLLBACK|empty|
|9 RECOVERY|empty|
|10 RESUME|empty|
|11 ABORT|empty; invalidates volatile upload session|
|12 VERIFY|slot:u32le; recompute payload CRC|

Status codes:0 OK,1 frame,2 state,3 range,4 CRC,5 version,6 I/O/ambiguous,
7 unsafe isolation,8 ownership conflict. These are ROM-management codes, never
NIO StatusCode. STATUS returns24u32le fields documented by FIELDS in rom_usb.py:
ABI/capacity, service/maintenance/core state, selected slot/generation, SRAM source/
length/CRC/fallback, upload/progress/faults, and cached validated slot descriptors.
STATUS never scans64KiB payloads during normal service. `served_*` fields identify
the SRAM-prepared image; only `active` indicates bus service enabled. Source2 is
compiled recovery. Fallback0 none,1 no usable metadata,2 explicit recovery,
3 bad selected binding/payload,4 unsupported physical flash capacity.

## Build, verification and automated tests

Use scripts/build-rom-usb.sh with absolute verified recovery ROM, manifest and a
NEW output directory. This builds only springboard_rom_usb; no hardware action.
The current candidate embeds the frozen Phase2 autoarm recovery ROM. The standard
frozen-engine verifier plus verify-rom-usb.py checks exact ARM body, unchanged
PIO instruction/template bytes (one allowed literal relocation), table alignment,
ELF/BIN/UF2 linkage, recovery payload and SRAM placement/partition symbols.

Native Debug/Release tests retain all previous suites and add test_rom_store,
including cuts at every upload erase/page/header boundary. test-rom-usb.py runs
the actual PC client through actual C wire/dispatch/store code against fake flash:
reset/interruption, CRC/size/schema/binding errors, activation with lost reply,
rollback, recovery, isolation loss, ordered nonce ownership and no retry. This is
not a simulation of QSPI/USB hardware. test-rom-usb-layout.py invokes GNU ld with
valid/oversized/incorrect-capacity layouts to exercise the actual ASSERT gate.
Compiled ARM/PIO and startup suites exercise the linked candidate separately.

## Exact engineer-run workflow — no automatic hardware action

Initial firmware deployment needs ONE manually confirmed RP2350 BOOTSEL flash.
No Prism reset or reinsertion is automatic. Candidate is ROM-only: do not run
CS1 diagnostics against it. Verify the supplied UF2 SHA before loading it. Preserve
installed UF2/evidence and prepare the frozen recovery UF2 as rollback. No logic
analyzer or routine scope capture is required for upload verification.

After candidate deployment, rediscover the dedicated USB serial port. Example
commands below are run by the engineer; user only performs cued physical actions.
Use a real verified ROM and nonzero version. Status alone is safe to request while
serving. Before other operations, arrange/confirm continuous hardware RESET hold
and inactive strobes; keep it held through QUIESCE, upload and activation.

```sh
ROM_PORT=/dev/ttyACM1  # rediscover; product identity is checked by the client
python3 scripts/rom_usb.py --port "$ROM_PORT" status
python3 scripts/rom_usb.py --port "$ROM_PORT" quiesce
python3 scripts/rom_usb.py --port "$ROM_PORT" upload --slot 0 --image /absolute/new.rom --image-version 1
python3 scripts/rom_usb.py --port "$ROM_PORT" verify --slot 0 --image /absolute/new.rom
# Upload already did independent full byte/SHA/CRC readback. Selection unchanged.
python3 scripts/rom_usb.py --port "$ROM_PORT" activate --slot 0
python3 scripts/rom_usb.py --port "$ROM_PORT" status
# Expect selected_slot0, matching served_length/CRC; active0/maintenance1.
python3 scripts/rom_usb.py --port "$ROM_PORT" resume
# Only after engineer confirms resume pending/ready: user releases held RESET.
python3 scripts/rom_usb.py --port "$ROM_PORT" status
# Expect active1 and matching SRAM source/length/CRC. No PalmOS discovery claim.
```

Choose the inactive slot from STATUS, not always0. Do not upload into selected0.
If no new activation is wanted after upload, leave selection unchanged and issue
RESUME explicitly after deciding which SRAM image to serve. Aborted/failed uploads
need explicit ABORT or fresh QUIESCE/rescan before restart; no automatic resume.

## Rollback and recovery

With reset isolation re-established and explicit QUIESCE:

```sh
python3 scripts/rom_usb.py --port "$ROM_PORT" rollback
python3 scripts/rom_usb.py --port "$ROM_PORT" status
python3 scripts/rom_usb.py --port "$ROM_PORT" resume
# Engineer confirms readiness, then user releases RESET.
```

If prior binding was overwritten/unavailable, use `recovery` instead of rollback:
this persists compiled source2 and fills its SRAM image, still without resuming.
With corrupt selected payload, next RP boot automatically validates/falls back
before service, reports fallback reason and keeps the bad slot unselected for
service. Bad/ambiguous metadata also boots compiled recovery. A valid-but-conflicting
metadata pair fails closed and may refuse management mutation; do not force an
ambiguous slot choice. Manual BOOTSEL fallback to the frozen known-good ROM UF2
ignores all storage metadata and provides recovery without erasing saved slots.
Do not use flash_nuke/full-chip erase. Future metadata repair requires an explicitly
reviewed procedure; no destructive wipe command is provided here.

Firmware flash fallback is a separately authorized user BOOTSEL action. Its exact
artifact/hash accompanies this build's recovery bundle. No automatic RP or Prism
reset/reinsertion is part of the utility. A firmware-only UF2 does not intentionally
address slot sectors; preservation over actual firmware reflashes remains a
hardware qualification item. Future firmware must retain the linker/storage layout.

**ROM activation is not PalmOS Setup discovery.** The bench already demonstrated
that soft reset can retain the old HsSU RAM copy despite changed flash ROM. Never
claim a new Setup/app installed from upload, activation, RP reboot, status or one
Prism soft reset. Verify ROM storage/service and PalmOS database discovery as
separate results. The retained-Setup investigation remains open.
