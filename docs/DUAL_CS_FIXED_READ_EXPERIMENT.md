# Experiment 2 — CS0 ROM and CS1 fixed read

Date: 2026-10-28. **Experimental candidate prepared; no hardware test performed.**

## Result and scope

The shared read-response engine now serves the unchanged SRAM ROM on CS0 and
constant **BEEF on CS1**. There is one data-output owner, the original alternating
request/reply/completion protocol, and direct PIO1 clamps for select ending,
OE ending and RESET. No CS1 writes, register map, mailbox, CPU interrupt, NIO,
USB forwarding or ESP32 functionality is implemented. Existing MSC/CDC remains.

Relevant host gates pass: original359 CS0 cases,320 focused dual-select cases,
native Release/Debug14/14 each, actual linked initializer/binary/startup checks,
and standalone Palm diagnostic build/static verification. The standalone PRC
has explicit MOVE.W accesses and a true MOVE.L ROM-pair test.

**GO for later explicitly authorized, controlled hardware validation**, with
known timing/placement limitations below. This is not hardware qualification.
The user expressly permits Experiment2 offline development while Experiment1's
full-ROM integrity/consecutive-read checks remain pending. If those checks
identify a regression, investigate it before considering Experiment2 qualified.

### Identity and preservation

| Item | Identity |
|---|---|
| Parent Experiment1 | `d38ccee2f1a15ec7862508a3ff1e5b0ea9549134` |
| Experiment2 source | `3c185dceed851bf3ad014a7264177d71287df10a` |
| Branch | `experiment/dual-cs-fixed-read` |
| Worktree | `/home/mozzwald/nio/dual-cs-fixed-read` |
| Original MSC source | `c41ec8c251edce6d4e1ce028c948404f88597d3c` |

Created the new worktree directly from Experiment1. No earlier unified prototype
was imported or used as a fixture. Before work, recorded Experiment1 branch,
HEAD, clean status,1,026 tracked source hashes and all6 prepared artifact hashes.
All remain identical afterward: [preservation proof](dual-fixed-read-evidence/preservation.json).
No merge, push, flash, device query, Prism reset/insertion, ROM update or probe
request occurred. No bench collector runs. Hardware was unavailable.

Experiment1's hardware result is **installation and idle RAM launch**, independently
checked by serial HotSync in the private plan commit44c26a19. It is not a new
full-ROM CRC, consecutive-read or direct-ROM-execution proof. Original October25
ROM execution and msc-003 evidence remain preserved separately.

## Exact implementation changes

Bridge-relative paths below refer to `bridges/rp2350-springboard/`:

| File | Change |
|---|---|
| `src/pio_rearm_program.c/.h` |32-word dual-pattern admission, expected region in Y, autopush request/completion, combined select guard and shared direct clamp, PIO0 conflict watchers. |
| `src/persistent_reply.S` | Move existing captured-control validation before ROM lookup. Out-of-line validated CS1 branch posts BEEF. Completion/deferred totals and adjacent FIFO/SIO captures retained. |
| `src/msc_main.c` | Start/park/restart the three actual PIO0 input SMs at their new entries; correct OE guard entry. Storage, loader, USB callbacks, activation and clock code unchanged. |
| `tests/test_pio_rearm.c`, `tests/persistent_cosim.c` | Name remapped preload/enable PCs; explicitly allow owner fallback29..31 while still prohibiting owner execution of guard21..28. No data, ownership, release or completion assertion removed. |
| `tests/dual_fixed_cosim.c` | Extend the original persistent fixture with region-aware selection/owner transitions, BEEF oracle and release observation. Retain strict original bounds and FIFO assertions. |
| `tests/dual_fixed_words.c`, host-only CMake additions | Reproducible standalone ARM test ELF and PIO co-simulation library. Neither test table nor library is linked into firmware. |
| `scripts/dual_fixed_harness.py`, `test-dual-fixed-read.py` | Existing mapped-register ARM harness with explicit CS1 expectations, unchanged359-case driver and focused cases. |
| `scripts/time-dual-fixed-read.py`, `verify-dual-fixed-read.py` | Matched-cost timing and exact actual initializer/ELF/BIN/UF2 checks. Earlier strict Experiment1 verifier remains unchanged. |
| `diagnostics/fixed-read/` | Standalone RAM PRC, explicit68000 access functions, CRC-protected result records/parser/tests, future serial HotSync helper and static verification. |

Full source/evidence inventory: [changed-files.txt](dual-fixed-read-evidence/changed-files.txt).
The report/evidence checkpoint follows the source commit; inspect branch log
for its final commit. Generated binaries remain in the isolated ignored build.

### Admission and requalification

Y starts at10. PCs0..2 retain MOV OSR,PINS; OUT X,4; JMP X!=Y. A CS0 match falls
straight through to the original IRQ-clear/settlement instruction. A mismatch
branches to29, which sets Y9 and accepts only a CS1 match. Any other nibble goes
to20, restores Y10 and retries. Both-low, both-high and write shapes are invalid.

Y is unchanged throughout the owned request, blocking PULL, Hi-Z preload and
post-reply compare. Thus postqualification checks the **original region**, not
whichever select happens to be low when ARM responds. PC20 restores Y10 only
after completion, using the original implicit wrap instead of an extra jump.

The two recovered words needed for guards come from full-word autopush:

- IN PINS,32 publishes the request in one instruction, replacing IN plus PUSH.
- IN X,32 publishes the completion in one instruction, replacing MOV ISR,X/PUSH.

Input GPIO visibility remains `APIO_IN_COUNT(30)`; that field is **not** the
push threshold. Autopush's zero-encoded threshold means32 bits. Each IN supplies
one complete32-bit word. No FIFO joining, extra record or packet format is added.
Request publication is one PIO instruction earlier; completion publication is
one earlier. The five delay slots at IRQ CLEAR remain unchanged.

### Core1 response

Core1 still reads high FIFO and low SIO in adjacent instructions. The captured
high/reset/control field is masked and compared with2000000A **before** SRAM
lookup. Normal CS0 then uses the same upper-address mask, index extraction,
16-bit word load and TX store. The three existing control-check instructions
were moved, not added to the normal path. Synthetic RX→TX costs remain10/14.

If the captured field is not A, the branch requires captured20000009 before
MOVW BEEF/STR TX. It never infers region from live GPIO. CS1 returns BEEF at any
address in its selected range; there is no register/address decode. Its unused
low SIO sample is retained to avoid disturbing the ROM capture sequence.

Malformed/reset-low captured requests still force sticky cancellation and post
one old-owned dummy0. Every completion must be0 orFFFFFFFF. The deferred outcome,
LR request total, ABI save/restore and exactly-one FIFO consumer are unchanged.
The CS1 side branch rejoins accounting after TX; no C call or per-read logging.

A delayed live low-address sample remains a limitation of the original ROM
architecture. This experiment does not introduce a latch/descriptor engine or
claim such a sample cannot occur. Output safety depends on sticky historical
ending and postqualification, as in Experiment1. Availability under arbitrary
CPU stalls is not guaranteed; a stale reply must be cancelled rather than driven.

## PIO allocation and actual instruction map

| Block | State machines | Instructions / responsibilities |
|---|---|---|
|PIO1/base16|SM0|Owner0..20 + fallback29..31. Sole output-enabling SM. |
|PIO1/base16|SM1|Combined CS0/CS1 ending guard21..26. Chooses a wait before blocking; IRQ/clamp23..24, wrap bottom21/top24. |
|PIO1/base16|SM2|OE high-level guard22..24; direct clamp, wrap22..24. |
|PIO1/base16|SM3|RESET27..28 → common23..24; direct clamp, wrap bottom27/top24. |
|PIO0/base16|SM0|Original WE assertion0..2 → NEXT IRQ0. |
|PIO0/base16|SM1/SM2|Shared3..6: symmetric conflicting-select assertion guards; no data output. |
|PIO0 SM3 / PIO2|unused|No new capture/backend workers. |

PIO1 **32/32 words,4 SMs**; PIO0 **7/32 words,3 SMs**; PIO2 unused. One output
owner remains; guards can only clear directions. GPIO mux, output range20..35,
clock divider, drive configuration and data preload semantics are checked against
Experiment1 by actual linked initializer execution.

| PC | Word | Instruction |
|---:|---|---|
|0|`A0E0`|MOV OSR,PINS|
|1|`6024`|OUT X,4|
|2|`00BD`|JMP X!=Y,29|
|3|`C540`|IRQ CLEAR0 [5]|
|4|`4000`|IN PINS,32 (request autopush)|
|5|`80A0`|PULL BLOCK|
|6|`7810`|OUT PINS,16 + preload marker|
|7|`A0E0`|MOV OSR,PINS|
|8|`6024`|OUT X,4|
|9|`00AF`|JMP X!=Y,15|
|10|`00CC`|JMP PIN,12 (RESET high)|
|11|`000F`|JMP15|
|12|`A06D`|MOV PINDIRS,~STATUS|
|13|`A02D`|MOV X,~STATUS|
|14|`0011`|JMP17|
|15|`E020`|SET X,0|
|16|`C000`|IRQ SET0|
|17|`20C0`|WAIT IRQ0 high (consume)|
|18|`B063`|MOV PINDIRS,NULL + marker low|
|19|`4020`|IN X,32 (completion autopush)|
|20|`E04A`|SET Y,10; wrap0|
|21|`00D9`|JMP PIN,25 (CS0 high)|
|22|`20A0`|WAIT PIN0 high (CS0/OE)|
|23|`C000`|IRQ SET0|
|24|`B063`|MOV PINDIRS,NULL + marker low|
|25|`2081`|WAIT GPIO1 high (CS1)|
|26|`0017`|JMP23|
|27|`2020`|WAIT PIN0 low (RESET)|
|28|`0017`|JMP23|
|29|`E049`|SET Y,9|
|30|`00B4`|JMP X!=Y,20|
|31|`0003`|JMP3|

PIO0:0 WAIT WE low;1 IRQ NEXT0;2 WAIT WE high;3 WAIT selected pin low;
4 JMP other pin high,6;5 IRQ NEXT0;6 WAIT selected pin high.
SM1 watches CS1 assertion against CS0; SM2 watches CS0 assertion against CS1.

### Cancellation and independent release

The combined guard waits on CS0 when it is low, otherwise CS1. Ending asserts
local IRQ0 and clamps pads in PIO1. OE and RESET independently share that clamp.
GPIO selection is used here for physical edge monitoring; the CPU's region is
always from its owned captured request. A CS1 assertion alone is valid when CS0
is high. Symmetric PIO0 assertion guards cancel when both are low, including a
conflict arising after output enable. WE cancellation remains unchanged.

No guard clears/consumes IRQ0 or touches RX/TX. Only SM0 consumes its ending and
later clears it at admission. Old cancellation stays with a blocked PULL through
a late TX and completion. A newer read cannot take the old reply. Direct clamps
operate without Core1 progress, and stop/fault pad overrides remain intact.

OE high or idle-high selects may repeatedly clamp in PIO; no CPU interrupt,
Core0 polling, SRAM publication or mailbox work is generated. Old idle clamps
settle under the original PC3 delay before any data enable. RESET still has its
explicit live qualification plus independent same-block clamp.

An initial smaller attempt forwarded OE ending to PIO0 and retained separate
select clamps. It failed the **existing CS0** OE-only-ending case after a six-
clock gap: old data remained driven when the next read began (error10 at128).
This is a credible regression at the observed gap scale, not a zero-gap stress
excuse. That version is rejected and never became a firmware candidate. Its
[source/log evidence](dual-fixed-read-evidence/rejected-oe-forward.txt) is retained
in the build's rejected-oe-forward directory. The final direct-OE-clamp version
passes that unchanged case and all original assertions.

## Test results

Offline tools: ARM GCC13.2.1, native GCC13.3, pinned SDK079c6f39/APIO1023d866/
epio bf49b19a, Unicorn2.1.4 and existing local Palm Docker image
`a96223e934a32bd2393996b2d8c7af5590795dd658c7c0cb28c201bc815346a5`.
No device was accessed. No earlier unified fixture/backend was imported.

| Gate | Result |
|---|---|
|Parent Experiment1 native Release|14/14 PASS before edits |
|Parent Experiment1 linked ARM/PIO control|359/359 PASS |
|Final native Release / Debug|14/14 each PASS |
|Final linked candidate original CS0 suite|359/359 PASS, original schedules/windows/outcomes retained |
|Final linked focused dual-select suite|320/320 PASS |
|Actual initializer/configuration/container/ROM verification|PASS |
|Linked boot/reset/stop/fault policy|8 cases PASS |
|Actual linked USB startup / MSC callbacks|PASS / PASS |
|Palm PRC metadata/opcodes|PASS,3776bytes, appl/FNFR |
|Palm result decoder tests|7/7 PASS |
|Physical Experiment2 / Palm diagnostic execution|UNTESTED |

The320 focused cases include192 paired phase/gap schedules (CS0/CS0, CS1/CS1,
CS0/CS1, CS1/CS0), four100-read runs,40 held-OE pairs,30 malformed/reset-low
shapes,48 ending-phase cases and six stalled-response cases. Gaps5..7 bracket
the previously observed gap range. No zero-gap or0..3-clock transition matrix
was introduced as a gate. Cases include differing expected data (CS1 BEEF →
CS0 A174) so stale BEEF cannot masquerade as a correct mixed read.

All existing FIFO framing, valid data, owner, release and ABI checks remain.
Changing the legal owner-PC set and preload/enable PC names reflects actual
instruction relocation; it does not relax the safety condition. Only the fixture's
new CS1 selection/data oracle changes. The original359-case driver is retained.
The mapped-register adapter avoids the known callback ITSTATE defect; these
results remain layout-specific emulation evidence, not universal simulator proof.

Detailed summaries/logs are under [dual-fixed-read-evidence](dual-fixed-read-evidence/).
Full synthetic traces and results remain in ignored `build/evidence`.
The actual linked kernel bytes equal the standalone host kernel that passed the
focused suite. The initializer verifier compares actual ARM-generated writes,
not only APIO's separate emulation path. No original strict Experiment1 verifier
was weakened; this new layout has a separate exact-opcode/configuration audit.

### Reproduction

From `bridges/rp2350-springboard` in this worktree, use new output directories:

```sh
cmake -S . -B build/dual-host -DBRIDGE_MODE=host -DCMAKE_BUILD_TYPE=Release
cmake --build build/dual-host --parallel 4
cmake --build build/dual-host --target dual_fixed_reply_elf
ctest --test-dir build/dual-host --output-on-failure
# Repeat native configure/build/ctest with dual-Debug / Debug.
uv run --offline --with unicorn --with pyelftools python scripts/test-dual-fixed-read.py \
 build/dual-host/dual-fixed-HOST-ONLY.elf build/dual-host/libdual_fixed_cosim.so \
 --regression --output build/NEW-cs0.json
uv run --offline --with unicorn --with pyelftools python scripts/test-dual-fixed-read.py \
 build/dual-host/dual-fixed-HOST-ONLY.elf build/dual-host/libdual_fixed_cosim.so \
 --output build/NEW-focused
scripts/build-rom-msc.sh \
 /home/mozzwald/nio/handspring-visor-plan/evidence/rom-management/2026-10-27-msc-002/recovery.rom \
 /home/mozzwald/nio/handspring-visor-plan/evidence/rom-management/2026-10-27-msc-002/recovery-manifest.json \
 "$PWD/build/fixed-firmware"
```

Repeat both Python gates using `build/fixed-firmware/springboard_rom_msc.elf` to
validate actual linked firmware (performed here). `verify-dual-fixed-read.py`
takes baseline Experiment1 build directory, candidate build directory, recovery
ROM, `--host-elf build/dual-host/dual-fixed-HOST-ONLY.elf`, and `--output FILE`.
`test-boot-autoarm.py ELF ROM --output FILE`, `test-msc-usb-startup.py ELF --output
FILE` and `test-msc-arm.py ELF --output FILE` are the unchanged existing tests.
Host-only ELFs are nonbootable and must never be flashed.

`time-dual-fixed-read.py BASE-ELF NEW-ELF BASE-LIB NEW-LIB --output NEW-DIR`
uses the Experiment1 read-only timing probe compiled into this worktree's build,
and the new co-simulation library. Same ARM costs, same68-clock selections,
6-clock gaps and eight phases. Baseline files were read only, never rebuilt
in the preserved Experiment1 worktree. Build identities are in build-id.json.

## Timing comparison — modeled, not measured

| Path/profile | Select→request | ARM RX→TX | Select→enable | Nominal release→Hi-Z |
|---|---:|---:|---:|---:|
|Exp1 CS0 /1|13–15|10|34–40|5|
|Exp2 CS0 /1|12–17|10|34–42|5|
|Exp2 CS1 /1|15–20|9|34–41|5|
|Exp1 CS0 /2|13–15|14|40–58|5|
|Exp2 CS0 /2|12–17|14|40–57|5|
|Exp2 CS1 /2|15–20|13|45–56|5|

Values are synthetic clocks. TX→preload is2 in the normalized edge convention;
preload→enable remains5. Release table concerns the nominal paired schedules;
select-only, OE-only, reset, conflict and late-response cases separately pass
the unchanged bounded-release assertions. No universal electrical limit follows.

CS0's ARM reply cost is unchanged; admission polling is not timing-equivalent.
The invalid-control loop now checks both patterns, changing phase. Matched CS0
enable deltas range **−6..+6 clocks** in profile1 and **−10..0** in profile2.
The profile1 maximum across the comparison grows40→42. Thus this report does
not claim CS0 timing equality or physical compatibility. The changes provide
required dual admission rather than inserting general dispatch/cache work into
ROM lookup. No clock,330ns setting or read window was changed to manufacture PASS.

The preserved CPU scheduling/protocol handles tested consecutive reads, including
fast inter-region transitions. Arbitrary stalls have no finite availability bound;
late responses must cancel safely. The real Prism sampling instant and M33/SRAM
arbitration costs are unmeasured. PIO clocks/marker edges are not setup/hold margin.

Kernel size changes288→306bytes; code address20002D34→20002DAC (Thumb symbols
20002D35→20002DAD). The64KiB table moves2000C508→2000C5A8, retaining modulo16=8.
No second table or backend allocation is added. SRAM placement and stack/ROM
bounds pass static checks; physical timing still needs validation.

## Palm diagnostic and mapping

The [standalone diagnostic sources](../bridges/rp2350-springboard/diagnostics/fixed-read/README.md)
are part of this isolated branch; `fujinet-palm` was not modified. It uses the
existing local Docker toolchain. Actual disassembly verifies:

```text
df_read_word: movea.l 4(sp),a0; moveq0,d0; MOVE.W (a0),d0; rts
df_read_pair: movea.l 4(sp),a0; MOVE.L (a0),d0; rts
df_pc:       lea df_pc(pc),a0; move.l a0,d0; rts
```

The module card/generation comes from the existing successful Module B install
receipt, not a hardcoded card or guessed address. Documented `HsCardAttrGet`
(selector6) attributes `hsCardAttrCsBase=3` and `hsCardAttrCsSize=4` are read as
UInt32; `cs1=cs0+cs_size` uses checked addition. Source references: the SDK audit
`handspring-visor-plan/docs/visor/RAM_DRIVER_PIVOT_AUDIT.md` §4 and existing
`fujinet-palm/palm/apps/visorbus/hs_card.h` / `cs1stagea/stagea.c`.

The utility checks presence, generation, mapping,330ns and its own card0 RAM
code heap/PC bounds. It restricts the observed16MiB-aligned Prism mapping because
the existing ROM decoder uses physical low address bits. It never writes timing
or executes from CS1. No explicit Springboard access occurs until a button.

- **CS1 once:** one MOVE.W at CS1 offset0; expect0000BEEF.
- **CS1 x100:**100 word reads, no intentional delay; expect allBEEF.
- **Alt x100:**100 CS0+8=FEED then CS1+0=BEEF iterations.
- **ROM pair x4:**four aligned MOVE.L at CS0+8; expectFEEDBEEF.

Each bounded run saves a CRC-protected96-byte record to `FN Fixed Read E2 Log`,
DATA/FNFR, including mode, mapping, counts, RAM PC, first mismatch and observed
value. The independent decoder rejects incomplete counts, wrong mapping/data or
bad CRC. Bus-error/removal catch-list handling encloses the read sequence.
No software test proves the exact physical spacing, output-enable edge or analog
bus ownership. Fixed values alone can hide residue; the alternating test changes
expected data and the host suite additionally checks independent owner events.

The app is `CS1 Fixed E2` / appl/FNFR, distinct from existing `CS1 Module B RAM`.
**Do not use Module B's Run four:** that old app expects write/ACK functionality
which this firmware deliberately lacks. The new PRC is standalone RAM software;
it has no reset-after-install flag and does not replace the module ROM image.

## Candidate artifacts and rollback

Directory:
`/home/mozzwald/nio/dual-cs-fixed-read/bridges/rp2350-springboard/build/artifacts/`

| File | SHA256 |
|---|---|
|`dual-cs-fixed-read-exp2.uf2`|`01de878c9f84a7e8c9fe045ff80cde5bd29112cfbf3e260cfeea00001fbcdc61`|
|`dual-cs-fixed-read-exp2.elf`|`50e9ef8bba2f4dd679bceadf4f915ab0cd29cb5824662c420e7aade2ed33c648`|
|`dual-cs-fixed-read-exp2.bin`|`ad7c017b04d271d972b6ff657844a4abd615fbc2ccab81c979e9f0508b53a6b6`|
|`cs1-fixed-e2.prc`|`a67f31b0b7a3314922d4e587dc3428759a8366bb9d85ed1beaf3a42379ae1d0e`|
|`rollback-exp1.uf2`|`fa36ced9c3cf2ad893f7cf390b95e4410c4a349799c090e0d355d7fc185b65d9`|
|`rollback-msc003.uf2`|`3005edefaf45cbf3ee180dd464f0cc3bcf936c7c6e54ae25a6963ee4b720167b`|

BUILD_ID.json/SHA256SUMS contain exact source/file identities. Stored ROM,
partition boundaries, embedded recovery image and USB descriptors are unchanged.
The legacy CDC product does not attest build identity; use the file hash and
manual flash record. Live per-request totals are not added to the CDC console.

## Later manual hardware procedure — no action requested now

Codex manages the PC tooling, logging and verified serial palm-sync, and sends
ntfy cues. The user performs only the explicitly cued button/physical actions.
No collector has been started and no hardware is required to finish this task.

1. Close Experiment1's pending bounded consecutive-read and complete-ROM
   readback/CRC checks. The new PRC's **ROM pair x4 only** can help with the former;
   use the established collector and exact current ROM reference for the full
   image. Do not tap CS1 buttons with Experiment1 firmware. Investigate a failure
   before calling Experiment2 hardware-qualified.
2. After explicit authorization, record current ROM identity/status and guide
   the established safe manual BOOTSEL/flash procedure for the experimental UF2.
   Do not recopy SPRINGBD.ROM, change waits/clock/pins or hold Prism reset.
3. Confirm USB/status and unchanged served-ROM identity. Cue only the agreed
   reset/discovery action; observe module recognition/install and launch. An
   old launcher icon alone is not proof of new ROM reads. No automatic reset or
   reinsertion. Stop on a crash or regression and retain evidence.
4. Install `cs1-fixed-e2.prc` using the prepared `diagnostics/fixed-read/bench.py
   install --prc ABSOLUTE-PRC --output NEW-DIR` with cached pyserial. The engineer
   starts it; user presses HotSync once. It installs only the RAM diagnostic.
5. Open **CS1 Fixed E2**, leave idle and report Err/RAM PC. Then cue **ROM pair
   x4**, expecting Err0, ValueFEEDBEEF, Done4, Bad0. If successful, cue **CS1 once**,
   expecting Err0, Value0000BEEF, Done1, Bad0. Do not repeat a failing run.
6. If earlier reads pass, cue **CS1 x100** and **Alt x100** once each: Err0, Done100,
   Bad0. These are bounded word loops, not proof of a measured MOVE.L CS1 gap.
7. One download-only serial HotSync using `bench.py collect --output NEW-DIR`
   retrieves results/app; independently decode records and compare app identity.
   Repeat full-ROM integrity on the candidate using the existing checker where
   available. Source/physical ROM execution, timing and lifecycle gates remain
   distinct. No scope/LA unless a specific failure makes it necessary.
8. For rollback, manually BOOTSEL/flash `rollback-exp1.uf2` or original
   `rollback-msc003.uf2`, keeping stored ROM unchanged. No automatic erase/reset,
   hard reset, repeated insertion or speculative image changes. Preserve logs.

Recommendation: keep this branch unmerged as a **minimal fixed-read hardware
candidate**. Advance no mailbox/NIO/write feature until the required physical
ROM and CS1 read checks pass. A later Experiment1 integrity regression takes
priority over further Experiment2 qualification.
