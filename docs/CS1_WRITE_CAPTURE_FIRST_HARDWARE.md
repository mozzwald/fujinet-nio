# Experiment 3 — first physical CS1 write captures

2026-10-28. **Two address/data captures match; write qualification remains incomplete.**
No mailbox/register effect, continuous write service, timing margin or complete
Experiment3 hardware qualification is claimed.

## Exact identities and preservation

Source `0e8597a799d9d875fea069ab1e4c7b1163a671f7`, branch
`experiment/dual-cs-write-capture`, worktree `/home/mozzwald/nio/dual-cs-write-capture`.
Flashed UF2 SHA256 `6b9bdc8c44b31d83df2f956ffb8ad7c078db77b85b822793237cf9eb449d15c7`.
Palm PRC SHA256 `e0af12537bf0b347ee616ebdb0a7593ad6effd1020aa5a97aa45aa899fa9982b`.
The downloaded app's complete resource map matches the prepared PRC.
Firmware, ROM and diagnostic code remained unchanged during all tests/recovery.
Stored ROM: slot1/version2,8842 bytes,CRC5228D802,sequence107.
Frozen Experiment2 and both earlier rollback images remain intact.

Raw evidence and independently decoded correlation live in the private plan:
`automation-sessions/write-capture-20261028/test-001/`.
`correlation.json` deliberately distinguishes raw data matching from acceptance.
Complete Saved Preferences downloads remain local; only the relevant FNMC/1
resource is archived with the evidence, avoiding unrelated preferences.

## Preserved read controls

Initial startup reached Launcher without alert. Saved read log sequences6/7
independently verify four explicit FEEDBEEF MOVE.L pairs (8CS0 halfwords), then
one BEEF CS1 read, all with error0/mismatch0. Generation9,330ns,RAM PC0005FAA4.
This is bounded functional evidence; the Experiment3 full-ROM readback and
physical response/release timing have not been measured.

## Initialization and lifecycle observations

After HotSync installation, write app showed Err1/RAM PC0. Both downloaded
read/write PRCs matched builds. Saved FNMC receipt was RP_REMOVE/ACTIVE0 at
generation9. Both diagnostics eventually showed the same initialization error;
no CS1 write had been attempted. The guard was retained, never bypassed.

A manually cued Prism reset produced MemoryMgr.c Line4390, Invalid handle.
Read-only RP status while alert held: active1, all reported faults0, same ROM,
write observer session0/phase0. One manual recovery reset returned normally to
time setting. Thus this was isolated, not consecutive; cause remains unknown.
Per user policy, record and recover after isolated alerts, pause the sequence
after two consecutive occurrences. No automatic reset, erase or hard reset.

Lifecycle download contains23 records. Generation9 installation succeeded,
then removal(record20). Generation10 reset generated install_begin(record21),
install_end(record22) with signed-extended FFFF9102/RP_COLLISION atstage2,
ACTIVE0, then removal(record23). Stage2 checks the existing RAM clone before
reuse. This does not identify which content/ownership/attribute check failed,
or establish a physical or firmware cause. Palm clock differs from host;
record/generation order is the reliable chronological reference.

User requested a controlled reseat and RP reset. Module removed, Prism stable
and responsive; RP-only physical reset while out, USB returned, same ROM, no
reported faults, active0. Source startup waits for CS0/OE release and RESET high;
this is consistent with disconnected idle inputs, not a measured pin state.
One insertion showed normal Please Wait then Launcher, RP active1/no faults.
One requested Prism reset then restored write app Err0/RAM PC0005DA2A.
Saved write records identify generation11. This recovery did not change code or
prove a connection fault; the stage2 reuse collision remains unexplained.

## First two stores

User opened confirmation before explicit PC arming. First W0 arm returned
bus-not-ready, session0/phase0, saved separately; no Write cue was issued.
One controlled second arm succeeded. W2 used one new arm. Each user pressed
Write exactly once; PC finished/stopped the capture promptly after the reply.

|Observation|W0|W2|
|---|---:|---:|
|Palm intent sequence|1|2|
|RP capture session|1|2|
|Offset, intended and captured|0 /1234|2 /A55A|
|Selected write pulses|1|1|
|Low/high/outcome FIFO depths|2/2/1|2/2/1|
|Low ending/high ending/outcome|15/13/0|15/15/0|
|Reset edge/FIFO fault|0/0|0/0|
|CS0 edge/OE edge history|1/1|1/1|
|Result|4/WC_CS0_ACTIVITY|4/WC_CS0_ACTIVITY|
|Qualified|false|false|

Both96-byte Palm records passed CRC and schema checks: Err0, exactly one issued
store per record, requested/completed1, no explicit diagnostic CS0 reads,
correct intended data/address, API-derived mapping and RAM proof,330ns.
RAM PC0005DA2A lies in the verified RAM resource. The PIO's immutable snapshots
independently match the two distinct intended addresses/values. No late CPU
GPIO sampling was substituted. No cancelled ending, missing frame, second
pulse, FIFO fault or reset was reported in either capture.

The validator **correctly rejects both entire arm windows** because CS0 and OE
falling edges were recorded somewhere between arm and finish. These are raw
historical flags without timestamps. They cannot establish whether a read was
background traffic before/after the store, an invalid overlap, or a transient
inside the write. Do not label them PASS, ignore the flags or claim electrical
ownership/timing qualification merely from matching data. No register, mailbox
or NIO command effect was implemented or committed by these stores.

## Final state and next step

Final CDC status after read-only HotSync: active1, faults0, same ROM, write
capture session2/phase2 stopped, result4/qualified0. Disk writes/flash_ops0 refer
to MSC storage, not the two observed CS1 stores. Both collectors completed;
no background bench process remains. No additional reset or write requested.

The next engineering problem is **transaction-scoped cancellation evidence**.
Before another candidate, identify how to distinguish CS0/OE invalidation during
the actual selected write from legitimate activity elsewhere in the arm window,
using concrete PIO/IRQ/resource timing. Preserve reset rejection, no read-drive
during writes, immutable address/data pairing, late cancellation, FIFO ownership
and the unchanged read owner. Do not simply clear or ignore window flags, widen
read windows or relax meaningful assertions to obtain PASS. No new firmware
or scope/LA experiment was authorized or initiated as part of this analysis.
Continuous acceptance/retirement and exactly-once register effects remain later
work. Repeating identical long arm windows will not resolve the missing timing
information. Any targeted physical measurement must first have a specific
question that existing captures cannot answer.
