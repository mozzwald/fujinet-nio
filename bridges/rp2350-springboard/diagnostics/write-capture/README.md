# CS1 Write E3 — first capture-only Palm diagnostic

Separate RAM application `CS1 Write E3`, appl/FNFW, versionE3.20261028.1.
It uses the existing Module B success preference and Handspring API queries;
CS1 is checked `cs0_base + cs_size`. It requires the observed Prism mapping,
330ns and RAM code/heap proof, with module-removal exception handling.
No ROM image change or module installer replacement is needed.

Open and leave idle **before** the PC arms a capture. On an explicit bench cue,
tap one button then **Write** in its confirmation:

- **W0 = 1234:**one MOVE.W of1234 to CS1+0.
- **W2 = A55A:**one MOVE.W ofA55A to CS1+2, requiring a separate PC arm window.

The helper reads its pointer/value parameters from RAM and performs exactly one
16-bit peripheral write, opcode signature`206f000430af00084e75`. There is no
peripheral read, byte store, implicit32-bit peripheral store or CS1 execution.
The screen says **SENT: await PC check**, showing intended data and issued count.
It never claims the RP accepted the write. No sound/extra bus test is performed.

Each96-byte big-endian CRC-protected record is appended to
`FN Write Capture E3`, DATA/FNFW. `decode.py` checks intended values, count,
mapping and RAM evidence, and always leaves `rp_capture_verified=false`.
Actual data and address must separately match the RP's immutable snapshots.
`test_decode.py` has seven tests. The first arm window permits exactly one
selected WE pulse; multiple writes invalidate it. Do not tap twice.

Engineer-only build, from the isolated worktree:

```sh
/home/mozzwald/nio/fujinet-palm/docker/palm-build.sh make -C bridges/rp2350-springboard/diagnostics/write-capture
```

Engineer-only bench tooling, when hardware testing is authorized:

```text
uv run --offline --with pyserial python bench.py install --prc ABSOLUTE-PRC --output NEW-INSTALL-DIR
uv run --offline --with pyserial python bench.py collect --output NEW-DOWNLOAD-DIR
```

These use the standing verified serial palm-sync method. The user only presses
HotSync when cued. They never reset, flash, activate a ROM or start an app.
Do not start HotSync until the engineer has finished/stopped the RP capture:
ROM/OE activity during a window conservatively invalidates the record. The
window must finish within60seconds; delay/expiry is logged and requires a new
explicit arm. No automatic retry or overwrite of an existing capture directory.

RP console helper is`../../scripts/write_capture.py`, with explicit status,
arm and finish subcommands. It confirms USB identity and capture ABI before
sending an arm, then checks session and actual snapshot data on finish.
This is a debug observation interface, not a NIO command or mailbox protocol.

An isolated PalmOS MemoryMgr alert is recorded and followed by explicitly cued
recovery/continuation under the user's policy. Two consecutive occurrences stop
that sequence. No automatic reset, repeated flash or erase action is included.
