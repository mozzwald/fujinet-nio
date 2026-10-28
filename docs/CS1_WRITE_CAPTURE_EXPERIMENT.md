# Experiment 3 — first CS1 write-capture candidate

2026-10-28. **GO for a manually authorized, controlled one-shot capture test.**
No Experiment3 firmware was flashed or tested physically during preparation.
This proves no production write/mailbox service yet; it prepares the smallest
write experiment without changing the hardware-working read engine.

## Frozen read checkpoint and isolation

Experiment2 is frozen at annotated tag`checkpoint/exp2-fixed-read-20261028`,
target`0b8186b2e56d68d6f7490b3f568bd7fb5c32b895`; its source implementation is
`3c185dceed851bf3ad014a7264177d71287df10a`. UF2 SHA256
`01de878c9f84a7e8c9fe045ff80cde5bd29112cfbf3e260cfeea00001fbcdc61`.
Physical read evidence is in private-plan commit`052bb5c6`: paired ROM reads,
CS1 single/repeated/alternating reads and byte-perfect8842-byte full ROM.
The unresolved isolated startup alert remains in that evidence.

User directs recording isolated MemoryMgr events, manually recovering and
continuing. Two consecutive occurrences stop that sequence for investigation.
This does not waive wrong-data, ownership or electrical-safety assertions.

New worktree`/home/mozzwald/nio/dual-cs-write-capture`, branch
`experiment/dual-cs-write-capture`, directly based on that frozen tag. Source
commit`0e8597a799d9d875fea069ab1e4c7b1163a671f7`. The original Experiment2
tree/artifacts remain exact and clean; no previous unified prototype imported,
no merge/push, no hardware action. Full source/artifact hash inventory is saved
in the planning repo's`evidence/dual-fixed-read/2026-10-28-exp2-frozen/`.

## Narrow implementation

Keep **CS0 ROM / CS1 fixed BEEF reads exactly as Experiment2**. Add an independent
input-only write observer, explicitly armed from the existing debug console.
One arm window permits exactly **one CS1-selected WE pulse**. Palm writes one
aligned16-bit value; the engineer stops capture and verifies actual latched
address/data. Nothing updates a peripheral register, ROM, command or mailbox.
The Palm screen therefore says **SENT**, never ACK/PASS on its own.

There is no Core1 write retirement, new read dispatch or live CPU address/data
sampling. No new interrupt handler, DMA, periodic Core0 work, UART during bus
accesses, second output owner, NIO command, USB forwarding or clock/wait change.
The new console functionality is equipment control/observation, not a replacement
FujiNet protocol. The existing USB MSC loader/partition/activation rules and
compiled-in recovery image are preserved.

The frontend stays disabled at boot, including during module recognition and
installation. Only explicit`write-arm` configures unused instruction words/SMs.
The actual read service/main startup is unchanged outside explicit console-line
dispatch. Finish disables only the observer, leaving the read responder active.

### One-shot ownership and cancellation contract

- Low address is latched in PIO2 while the sampled write controls equal5.
- PIO0 latches the high window, including all16 data bits, high address bits and
  reset. It updates from coherent, already-qualified pin snapshots during the
  write, retaining the last valid sample before ending. No post-WE live sample
  is used to reconstruct data. MarkerGPIO39 is ignored by address reconstruction.
- Separate PIO2 CS1-release and WE-ending machines latch historical outcome.
  Equal sampled CS1/WE endings are accepted; CS1 ending one observed clock
  earlier is rejected. The outcome is not inferred from CPU-side idle levels.
- Both samplers halt after their first frame. An independent selected-WE counter
  continues counting; any second pulse rejects the entire arm window. Thus a
  missing half cannot be paired with a later transaction and called valid.
- Core0 does not drain anything until explicit finish. Low/high FIFO depths2,
  outcome depth1 fit their private four-word FIFOs even if Core0 stalls.
- Raw GPIO falling-edge history for CS0, OE and RESET is cleared at opening and
  inspected at finish. **Any** such activity invalidates this diagnostic window,
  even if after the write. This conservatively catches short invalidation pulses
  and reset independently of sampler polling. No GPIO IRQ is enabled.
- This is an **exclusive diagnostic window**, not a continuous bus service.
  Do not run ROM/CS1 read tests, HotSync, reset or additional writes between arm
  and finish. Background ROM/OE activity may abort the capture; that is retained
  evidence, not a silently discarded peripheral command. Valid captured fields
  remain available even when qualification rejects the window.
- Finish within60seconds. The counter needs at least five150MHz PIO clocks per
  increment; a minute bounds its count below2^32. Expiry rejects the record.
  There is no periodic timeout task or interrupt.
- Arm requires active/fault-free ROM service and idle/released controls, checked
  again after initialization. Double arm fails; a failed final idle check keeps
  the previous saved record. RP reset/session changes are rejected by the host.

No command side effects occur before or after outcome validation. Future
continuous writes will need their own queue/epoch contract and cannot treat
this one-shot observer as a finished mailbox implementation.

## Resource allocation and instruction map

| Block/SM | Before | Experiment3 |
|---|---|---|
|PIO1 SM0|Shared ROM/BEEF read owner|Unchanged; sole positive data-output owner |
|PIO1 SM1/2/3|Select/OE/reset independent clamps|Unchanged |
|PIO0 SM0/1/2|WE/conflicting-select read cancellation|Unchanged words0..6 and configuration |
|PIO0 SM3|Unused|Input-only high/data sampler, words7..27; GPIOBASE16 retained |
|PIO2 SM0|Unused|Input-only low/address sampler, words0..13; GPIOBASE0 |
|PIO2 SM1|Unused|Write-ending outcome, words14..19 |
|PIO2 SM2|Unused|CS1-ending sticky history, words20..22 |
|PIO2 SM3|Unused|Selected-WE pulse counter, words23..31; wrap23..27 with28..31 tail |

Total words: PIO0 **7→28**, PIO1 **32 unchanged**, PIO2 **0→32**.
Free words: PIO0 addresses28..31 only. All12 SMs are allocated while armed;
five new input SMs are disabled before arm/after finish. No output pins, pad
muxes, drive strength, baseline GPIOBASE or read IRQ/FIFO ownership are altered.

Actual new program map (addresses are within the indicated block):

|PIO0 address|High/data sampler instruction|
|---:|---|
|7|MOV ISR,PINS |
|8|MOV OSR,ISR |
|9|NOP |
|10|OUT X,4 |
|11|JMP X!=Y,7 |
|12|JMP RESET-released,14 |
|13|JMP27 |
|14|NOP |
|15|MOV OSR,PINS |
|16|NOP |
|17|OUT X,4 |
|18|JMP RESET-released,20 |
|19|SET X,0 |
|20|JMP X!=Y,24 |
|21|MOV ISR,OSR |
|22|IN Y,4 (restore captured/validated control nibble5) |
|23|JMP15 |
|24|PUSH BLOCK (retained coherent snapshot) |
|25|MOV ISR,X |
|26|PUSH BLOCK (ending control) |
|27|WAIT IRQ7 (private permanent halt until explicit rearm) |

|PIO2 address|Instruction|
|---:|---|
|0..5|MOV ISR,PINS; MOV OSR,ISR; OUT NULL,16; OUT X,4; JMP X!=Y,0; PUSH BLOCK |
|6..10|MOV OSR,PINS; OUT NULL,16; OUT X,4; JMP X!=Y,11; JMP6 |
|11..13|MOV ISR,X; PUSH BLOCK; WAIT IRQ7 |
|14..19|WAIT IRQ2; WAIT WE-high; MOV ISR,STATUS(IRQ3); PUSH BLOCK; IRQ SET4; WAIT IRQ7 |
|20..22|WAIT CS1-low; WAIT CS1-high; IRQ SET3 (wrap20) |
|23..27|WAIT WE-low; JMP CS1-high,28; JMP X--,26; IRQ SET2; WAIT WE-high (wrap23) |
|28..31|MOV OSR,PINS; OUT Y,1; JMP !Y,24; JMP23 |

SM3 counter's IN_BASE19 makes the low MOV-PINS bit the WE level. Its tail allows
WE-before-CS1 ordering. All new IRQ flags are block-local: PIO2 flags2/3/4/7,
PIO0 halt7; no cross-PIO IRQ is emitted by this observer. PIO1 IRQ0 remains solely
the existing read/cancel protocol. Actual compiled initializer writes and opcode
destinations are audited, including no SET/OUT/MOV pin output operations.

## Timing and placement

Read request/reply/completion instructions, preload, enable and independent
release programs are **byte/source-identical** to the frozen read checkpoint.
The ARM worker remains306bytes with hash
`48ac86c72595ce712d7c42f44de26620b980e4dc0cbf8a32d24f325ee9ac5ced`.
Matched64 schedules (both prior cost profiles,8 phases, all CS0/CS1 pairs)
have identical request, TX, preload, enable, release and completion event times:
maximum modeled enable difference **0 clocks**.

This is not a physical timing-equivalence claim. Linking the console addition
moves the worker from`20002DAC` to`20002E18`, and the word table from
`2000C5A8` to`2000D0C8`. The64KiB word table keeps modulo16=8 SRAM alignment.
Final BSS end`20040C50` leaves substantial space below`20078000` and the8KiB
combined stack reservation; linker flash end`1000D1CC` stays below1MiB.
Real instruction-fetch/SRAM arbitration and physical setup/hold are unmeasured.

Write data sampling is independent of CPU response timing. The high sampler
updates from a coherent control/data snapshot every8 PIO clocks in its steady
loop; low controls poll every5. These are instruction counts, not measured
Prism data setup or minimum supported write timing. Two physical contrasting
word/address writes are the next gate; no throughput claim is made.

## Tests actually performed

|Gate|Actual result|
|---|---|
|Frozen baseline source/artifact/tag preservation|PASS; source hashes and every prepared artifact rechecked |
|Before changes native baseline|14/14 Release |
|Native final Debug / Release|15/15 each; expanded write test200 cases passes in both |
|Write capture data/address/phases/cancel/extra pulses|200 cases PASS; zero positive data-bus direction |
|Actual linked original CS0 matrix|359/359 unchanged assertions/windows |
|Actual linked focused dual-read matrix|320/320 unchanged assertions/windows |
|Read/write/read transitions at5..8-clock modeled gaps|256/256 PASS; read safety separate from capture-window qualification |
|Matched Exp2 vs Exp3 observable read scheduling|64/64 exact; max modeled enable delta0 |
|Actual linked Core0 management/SDK/formatter|17 cases PASS; before/after owned read configuration equal, no PIO1 writes |
|Actual linked read initializer / SRAM / ELF-BIN-UF2 / ROM / partitions / USB descriptors|PASS using unchanged read verifier |
|Actual linked boot policy / stop / reset / fault|8/8 PASS |
|Actual linked USB IRQ startup and MSC storage callbacks|PASS |
|Host capture client / Palm intent journal|8/8 and7/7 PASS |
|Palm PRC metadata/opcodes|PASS; one explicit MOVE.W store, PC-relative RAM location probe, no reset flag |
|Physical CS1 writes/timing|**UNTESTED** |

Source/tests live under`bridges/rp2350-springboard`. Commands are preserved in
the scripts/build helper; useful reproductions:

```sh
cmake -S . -B build/NEW-host -DBRIDGE_MODE=host -DCMAKE_BUILD_TYPE=Release
cmake --build build/NEW-host --parallel 4
ctest --test-dir build/NEW-host --output-on-failure
scripts/build-write-capture.sh ABS-RECOVERY-ROM ABS-MANIFEST ABS-NEW-BUILD-DIR
uv run --offline --no-project --with unicorn --with pyelftools python scripts/test-write-runtime.py ACTUAL-ELF --output NEW-JSON
python tests/test_write_capture_client.py
python -m unittest discover -s diagnostics/write-capture -p test_decode.py
```

The old read-layout verifier expects`springboard_rom_msc.*` filenames. Internal
`build/read-verifier-alias/` symlinks point at the actual new ELF/BIN/UF2 solely
to reuse that unchanged verifier; public artifacts carry the distinct Exp3 name.
The baseline argument is the frozen Exp1 layout the read verifier was designed
for; separate exact Exp2-vs-Exp3 scheduling/source checks establish continuity.

Two rejected host runs are retained in the investigation notes: APIO's enable
macro replaces a block mask, so the early host-enable8 disabled old PIO0 guards.
The unchanged focused suite caught a release violation; host enable now retains
mask15, while actual firmware uses SDK enable/disable on SM3 only. No failing
assertion was removed. Separately, global overlapping Unicorn RAM read/write
hooks made even no-op-hook plain-RAM snprintf return a function pointer. Plain
and MMIO-only controls return the correct5; final hooks are restricted to MMIO.
This rejected backend result did not justify changing firmware formatting.

## Artifacts

Under`/home/mozzwald/nio/dual-cs-write-capture/bridges/rp2350-springboard/build/artifacts/`:

- `write-capture-exp3.uf2` — separately identified experimental firmware.
- `cs1-write-e3.prc` — separate RAM write diagnostic; no replacement module ROM.
- Exact ELF/BIN,`BUILD_ID.json`, source/flags/hash manifest and`SHA256SUMS`.
- `rollback-exp2.uf2`, plus original Exp1 and MSC rollback copies.

Hashes:

```text
UF2 6b9bdc8c44b31d83df2f956ffb8ad7c078db77b85b822793237cf9eb449d15c7
ELF db210d1018812dea080ac798542bd9417a08ee293fc807b6c48f88ed4cdd9b2b
BIN 45438c3d947541915845107ffb7dd8faaea3882f2413e7b6f18bb08f697a2337
PRC e0af12537bf0b347ee616ebdb0a7593ad6effd1020aa5a97aa45aa899fa9982b
```

Compact machine-readable evidence is in`docs/write-capture-evidence/`; complete
host traces remain separately in the ignored build directories. No baseline
artifact was rebuilt, overwritten or relabeled as a new result.

## Manual hardware procedure — not initiated

The engineer manages every PC-side step and ntfy cue. User only performs the
explicitly cued buttons. No scope/LA or new ROM file is required.

1. Verify current ROM/status and both rollback hashes. After explicit test
   authorization and user-confirmed RP2350 BOOTSEL, engineer copies only the
   Exp3 UF2. Keep module seated; never hold Prism reset. Do not recopy the ROM.
2. Verify USB/status and unchanged active ROM. Cue at most the agreed brief
   Prism reset for startup. Log any MemoryMgr event; recover/continue after an
   isolated occurrence. Two consecutive occurrences pause the sequence. Do not
   automatically rollback or infer a cause from a single alert.
3. Establish the preserved read controls using the already-installed
   `CS1 Fixed E2` (ROM pair x4 and one CS1 read). Never old Module B Run four.
4. Engineer starts serial palm-sync install of`cs1-write-e3.prc`; user presses
   HotSync. Open **CS1 Write E3** and leave idle; verify Err0/RAM PC before arm.
5. Engineer runs`write_capture.py arm --port PORT --directory NEW-DIR
   --expected-address 0 --expected-value 0x1234`, using cached pyserial.
   After arm confirmation/ntfy, user taps **W0 = 1234 → Write once** and reports
   **SENT**, Err0, Intend1234, Issued1, Offset0. No other test/HotSync/reset.
6. Engineer immediately runs`write_capture.py finish --port PORT --directory
   SAME-DIR`, within60seconds. Require one pulse, complete immutable low/high/
   outcome fields, no fault/invalidation history and actual offset0/value1234.
   Preserve rejected frames and explicit reasons; never interpret SENT as ACK.
7. If successful, one separately armed/cued **W2 = A55A** test verifies offset2
   and changing data. New directory/session, same firmware. Do not run100 writes
   or demand immediate scratch readback: CS1 reads remain BEEF by design.
8. With input capture stopped, one read-only HotSync retrieves both Palm intent
   records and app for comparison with the two RP captures. Use existing full-ROM
   checker/collection if needed to close the new candidate's read-regression
   gate; old Exp2 captures alone are not Exp3 hardware evidence.
9. If output/data/ownership regresses or MemoryMgr repeats consecutively, stop
   and preserve evidence. Manual BOOTSEL restoration of`rollback-exp2.uf2` is
   available, keeping the ROM. No automated reset/reinsertion/erase operation.

**Recommendation:** proceed only with this bounded input-capture proof. First
obtain real write data and ending behavior while preserving reads. Keep streaming
write retirement, mailbox semantics, NIO and USB forwarding out of this experiment.
