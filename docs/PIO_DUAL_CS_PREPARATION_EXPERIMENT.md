# Experiment 1 — PIO dual-CS preparation

2026-10-27. **GO for one explicitly authorized, controlled CS0-only hardware test.**
Hardware compatibility is untested. Nothing was flashed, reset, reinserted or
uploaded; no device was opened and no scope/LA measurement was requested.

## A. Executive summary

Recovered **three usable PIO1 words, addresses29–31**, by compacting only the
existing same-block watchers. PIO1 now loads29 instructions instead of32.
The read owner's23 instructions, PCs0–22, are byte-identical. Core1 assembly,
ROM values, FIFO protocol, clocks, pins, USB/MSC loader and activation policy
are unchanged. CS1 still cancels a ROM read; no CS1 functionality was added.

The existing CS0/OE watchers retain direct PIO1 pad clamps. They now wait for
high, assert cancellation, clamp, then wait for low before watching high again.
RESET shares the IRQ/clamp tail and loops while asserted. There is no cross-PIO
relocation, extra CPU work or additional output enabler.

Baseline and candidate pass the existing359-case compiled response suite and
14 native Release tests. Candidate Debug passes14/14. A mapped-register control
also passes359/359 for each. All48 paired timing comparisons have identical
request, reply, preload, enable, release and completion edges.39 targeted guard
scenarios run on each engine (78 runs) pass with existing safety assertions.
Actual linked initializer execution confirms only the intended watcher changes.

Baseline rebuild is byte-identical to archived msc-003. Candidate preserves the
288-byte ARM kernel at the same address; its SRAM ROM table shifts16bytes while
retaining size and modulo16 bank alignment. This placement difference and the
changed idle watcher state remain explicit physical qualification concerns.

### Identities and isolation

- October25 milestone: `76f02f01e3b5be385b83aa81020eb7e60a138df6`.
- Complete MSC source baseline: `c41ec8c251edce6d4e1ce028c948404f88597d3c`.
- Experimental source: `91f5c1ad63a3eb911b10d371985912b1f8978264`.
- Branch: `experiment/pio-dual-cs-preparation`.
- Worktree: `/home/mozzwald/nio/pio-dual-cs-preparation`.
- Report/evidence are committed separately after the source commit; use
  `git log experiment/pio-dual-cs-preparation` for the final checkpoint.

Verified both baseline hashes and exact source equality of `persistent_reply.S`
and `pio_rearm_program.c` between76f02f0 and c41ec8c. Recorded original visor
branch/HEAD/dirty status and1,090 file hashes before creating the new worktree
**directly from c41ec8c**. No original branch switch, stash, reset or discarded
work. The earlier unified experiment was read for background only; none of its
prototypes, responders, descriptor engines or tests was imported.
See [preservation record](pio-preparation-evidence/preservation.json).

## B. Baseline architecture

The actual working source and rebuilt firmware are the reference:
`bridges/rp2350-springboard/src/pio_rearm_program.c`, `.h`,
`persistent_reply.S`, `boot_autoarm_entry.S`, and `msc_main.c`.

| Resource | Baseline allocation |
|---|---|
| PIO1/base16 SM0 | owner PCs0–22; RX0 request/completion, TX0 replies; sole positive enable |
| PIO1/base16 SM1 | CS0 watcher, entry23, wrap23..29 |
| PIO1/base16 SM2 | OE watcher, entry23, wrap23..29 |
| PIO1/base16 SM3 | RESET entry30, JMP26 to common tail, wrap bottom30/top29 |
| PIO0/base16 SM0/1 | WE/CS1 invalidation, shared words0–2, NEXT IRQ0 to PIO1 |
| PIO0 SM2/3; PIO2 all | unused by this baseline |
| Core1 | persistent RX-first SRAM lookup, exactly one FIFO consumer/publisher |
| Core0 | existing MSC/CDC/flash/lifecycle work, no new task |

Original guard words23–31 are WAIT GPIO0 low, WAIT PIN0 low, WAIT PIN0 high,
IRQ SET0, MOV PINDIRS,NULL, WAIT IRQ0 low, MOV Y,Y, WAIT PIN0 low, JMP26.
CS0/OE watchers share23–29; RESET enters30–31 and branches to26.

Main qualification remains MOV OSR,PINS; OUT X,4; JMP X!=Y,0 with Y10. Only
CS0/CS1/OE/WE nibbleA is accepted. High address/control/reset capture enters RX;
adjacent low SIO sample indexes the64KiB native-word table. Upper-address mask,
captured RESET/control validation and returned data are unchanged.

One request owns its blocking PULL, one TX and its completion0/FFFFFFFF. Sticky
IRQ0 prevents a late response enabling after cancellation. The persistent ARM
worker validates completion before consuming a subsequent request; deferred
totals remain after TX. No per-read restart, FIFO flush or CPU pad release.

PIO1 guards clamp independently. PIO0 WE/CS1 watchers signal IRQ0 without
owning pads. Only SM0's WAIT-high IRQ0 consumes the ending; owner PC3 explicitly
clears it with its original **five delay slots** (six clocks including IRQ CLEAR).
Reset/stop C pad overrides and park/start ordering are unchanged.

## C. New architecture and budget

No watcher moved to a different PIO. This avoids extra synchronization or a
cross-PIO clamp handshake. All four PIO1 SMs and two PIO0 SMs remain allocated.

| Block/SM | Before instructions | After instructions | Responsibility / signaling |
|---|---|---|---|
| PIO1 SM0 |0–22|0–22, identical|ROM owner; local IRQ0; original output enable |
| PIO1 SM1 |23–29|23–26, wrap23..26|CS0 ending → local IRQ0 → direct clamp |
| PIO1 SM2 |23–29|23–26, wrap23..26|OE ending → local IRQ0 → direct clamp |
| PIO1 SM3 |30–31 plus26–29|27–28 plus24–25, wrap bottom27/top25|RESET assertion → JMP24 → IRQ0/clamp, then return27 |
| PIO0 SM0/1 |0–2|0–2, identical|WE/CS1 → NEXT IRQ0 in PIO1 |
| PIO0 SM2/3 |free|free|unchanged |
| PIO2 all |free|free|unchanged |
| PIO1 instruction memory |32 used /0 free|29 used /3 free|**29,30,31 available** |
| PIO0 instruction memory |3 used /29 free|same|unchanged |

SM3's branch outside its wrap interval is intentional. Hardware wraps only
when executing the configured top instruction25, then goes to27. The baseline
already uses a similar bottom-above-top arrangement. The actual emitted wrap
fields and start instruction are checked, not inferred from comments.

### Why the removed waits/delay are not needed in this layout

The guard now watches complete high→low→high cycles. At idle-high it conservatively
asserts cancellation/clamp once, then stalls on low. After observing low it can
assert again only when that same pin becomes high. It never acknowledges or
clears IRQ0, so a late CPU response retains the old cancellation history.

When both required strobes become low and qualification can first succeed,
any previously observed high can leave at most its IRQ/clamp instructions
outstanding. These finish within two subsequent SM steps. The unchanged owner
must execute its qualification and PC3 clear plus five delay slots before
snapshot publication, and much later before enable. Thus there is no old-high
clamp outstanding into the valid data phase. An intervening new high is a real
ending and should cancel. CS-before-OE and OE-before-CS are both covered.

This replaces the original CS0 pre-gate, duplicate low wait, IRQ-low acknowledgment
and NOP with a reoriented low wait. The acknowledgment/NOP is removed **only from
the watcher**, not the owner's settlement delay or ownership protocol. The guards
can recognize subsequent endings while a CPU reply is delayed; IRQ0 stays sticky
until the owner consumes/clears it. A stalled Core1 is not needed for release.

RESET's direct assertion→JMP→IRQ→clamp sequence is retained. While held low it
reasserts/clamps autonomously every four PIO instructions, rather than waiting
for owner acknowledgment. When released it stops at its low wait. This is PIO
activity only, with no CPU interrupt or periodic ARM work. Post-reply RESET
qualification remains identical.

## D. Exact changes and resulting map

Only two production files changed:

1. `src/pio_rearm_program.c`: compact guard tail, per-SM wraps/entry,29-word load.
2. `src/pio_rearm_program.h`: RESET entry30→27; instruction count32→29.

Three focused test/audit files were added:

3. `tests/pio_preparation_probe.c`: read-only instruction accessor/release-event
   observer around the existing strict PIO fixture; no changed assertions.
4. `scripts/test-pio-preparation.py`: before/after timing and guard tests; a small
   ordinary mapped-register adapter for the unmodified existing359-case suite.
5. `scripts/verify-pio-preparation.py`: independent actual linked initializer,
   expected guard opcode/configuration and ELF/BIN/UF2/ROM/SRAM audit.

Documentation and evidence are under `docs/`. No ARM, boot shim, ROM, MSC/USB,
GPIO mapping, clock, CMake, mailbox or CS1 implementation was changed.
See the [complete inventory](pio-preparation-evidence/changed-files.txt).

### Actual PIO1 program

| PC | Actual word | Instruction |
|---:|---|---|
|0|`A0E0`|MOV OSR,PINS|
|1|`6024`|OUT X,4|
|2|`00A0`|JMP X!=Y,0|
|3|`C540`|IRQ CLEAR0 [5]|
|4|`4000`|IN PINS,32|
|5|`8020`|PUSH BLOCK|
|6|`80A0`|PULL BLOCK|
|7|`7810`|OUT PINS,16 + preload marker|
|8|`A0E0`|MOV OSR,PINS|
|9|`6024`|OUT X,4|
|10|`00B0`|JMP X!=Y,16|
|11|`00CD`|JMP PIN,13 (RESET)|
|12|`0010`|JMP16|
|13|`A06D`|MOV PINDIRS,~STATUS|
|14|`A02D`|MOV X,~STATUS|
|15|`0012`|JMP18|
|16|`E020`|SET X,0|
|17|`C000`|IRQ SET0|
|18|`20C0`|WAIT IRQ0 high (consume)|
|19|`B063`|MOV PINDIRS,NULL + marker low|
|20|`A0C1`|MOV ISR,X|
|21|`8020`|PUSH BLOCK|
|22|`0000`|JMP0|
|23|`20A0`|WAIT PIN0 high|
|24|`C000`|IRQ SET0|
|25|`B063`|MOV PINDIRS,NULL + marker low|
|26|`2020`|WAIT PIN0 low|
|27|`2020`|WAIT PIN0 low (RESET)|
|28|`0018`|JMP24|
|29–31|not loaded|free|

## E. Tests actually performed

Firmware compiler: arm-none-eabi GCC13.2.1, Cortex-M33, Release/O3. Native GCC13.3.
Pinned SDK079c6f39, APIO1023d866, epio bf49b19a, Unicorn2.1.4. Original150MHz,
330ns configuration; no physical timing measurement. Builds/tests were offline.

| Test | Baseline | Final candidate | Evidence |
|---|---|---|---|
| Native full CTest Release |14/14 PASS|14/14 PASS|`baseline-release.txt`, `candidate-release.txt` |
| Native full CTest Debug |not rerun for baseline|14/14 PASS|`candidate-debug.txt` |
| Original compiled ARM/PIO suite, unchanged callbacks/assertions |359/359 PASS|359/359 PASS|`baseline-callback-summary.json`, `candidate-arm-pio-summary.json` |
| Same entire suite with mapped-register adapter |359/359 PASS|359/359 PASS|`mapped-full.json`; full examples retained in build output |
| Paired timing/ARM event comparisons |48 schedules|48 identical schedules PASS|`timing-summary.json`; full data in build output |
| Targeted guards / idle strobes / CPU stalls |39 scenarios PASS|39 scenarios PASS|78 total runs, same focused output |
| Actual linked initializer / opcode/configuration differential |32-word reference|29words;23 owner words identical; allowed watcher changes only PASS|`firmware-verification.json` |
| ELF→BIN→UF2, embedded ROM, SRAM, partitions, USB descriptors |baseline UF2 equals archived msc-003|PASS|same verification report and `build-id.json` |
| Actual Core1 boot/idle/reset/stop/fault policy |baseline existing evidence|8 cases PASS|`candidate-boot.json` |
| Actual linked SDK USB initialization |baseline existing evidence|PASS|`candidate-usb.json` |
| Actual linked MSC callbacks/vector repair/fault clamp |baseline existing evidence|PASS|`candidate-storage-callbacks.json` |
| Physical recognition / application / CRC / lifecycle |historical proven baseline|**UNTESTED**|no hardware actions |

Evidence names are relative to [pio-preparation-evidence](pio-preparation-evidence/).

The native rearm tests retain2,250 CPU scheduling/gap combinations,120 OE lead/
lag cases,10,080 cancellation/late-response phases,80 CPU-stall phases,200
changing/repeated reads and held RESET; both existing marker variants pass.
Native tests already exercised short CS0 gaps; no assertion was relaxed. The
previous unified0–3-clock CS0/CS1 transition tests were not imported or made a
new gate for this CS0-only experiment.

The359-case suite retains signature/contrasting/0000/FFFF/repeated/upper-address
pairs, ending phases, stalls, held reset, invalid snapshots, bad completion and
100 continuous reads. The two normal timing profiles retain their expected
output enables; profile3 remains its original slower stress/control profile.

The focused helper adds only observation around the existing strict PIO model.
Guard runs cover CS0,OE,RESET,WE,CS1 endings; unselected OE/WE/CS1 strobes; and
CPU stalls after data enable. Existing FIFO, correct-word, owned-reply, bounded
release and ABI assertions remain. No synthetic data response substitutes for
actual compiled ARM TX.

The committed baseline harness uses MMIO callbacks. Given previously documented
Unicorn ITSTATE defects, both complete suites were additionally run with ordinary
mapped-register hooks. Only register interception changes; original schedules,
code and assertions remain. No prior unified framework is imported. These
results are layout-specific emulation evidence, not proof of simulator or
physical timing universality.

### Commands and retained outputs

All paths below are inside the new worktree except the immutable ROM inputs.
Baseline host/firmware were built **before editing**. Their build directories
are preserved; do not rebuild them against subsequently modified source and
call that a baseline. For a fresh reproduction, export c41ec8c into a separate
ignored source directory, link the same pinned dependencies, and build it there.

```sh
cd /home/mozzwald/nio/pio-dual-cs-preparation/bridges/rp2350-springboard
cmake -S . -B build/baseline-host -DBRIDGE_MODE=host -DCMAKE_BUILD_TYPE=Release
cmake --build build/baseline-host --parallel 4
ctest --test-dir build/baseline-host --output-on-failure
scripts/build-rom-msc.sh \
 /home/mozzwald/nio/handspring-visor-plan/evidence/rom-management/2026-10-27-msc-002/recovery.rom \
 /home/mozzwald/nio/handspring-visor-plan/evidence/rom-management/2026-10-27-msc-002/recovery-manifest.json \
 "$PWD/build/baseline-firmware"
# After the isolated PIO edit:
cmake -S . -B build/experimental-host -DBRIDGE_MODE=host -DCMAKE_BUILD_TYPE=Release
cmake --build build/experimental-host --parallel 4
ctest --test-dir build/experimental-host --output-on-failure
# Repeat candidate native configure/build/ctest with experimental-Debug / Debug.
scripts/build-rom-msc.sh \
 /home/mozzwald/nio/handspring-visor-plan/evidence/rom-management/2026-10-27-msc-002/recovery.rom \
 /home/mozzwald/nio/handspring-visor-plan/evidence/rom-management/2026-10-27-msc-002/recovery-manifest.json \
 "$PWD/build/preparation-firmware"
uv run --offline --with unicorn --with pyelftools python scripts/test-persistent-reply.py \
 build/baseline-firmware/springboard_rom_msc.elf build/baseline-host/libpersistent_cosim.so \
 --output build/evidence/baseline-callback.json
uv run --offline --with unicorn --with pyelftools python scripts/test-persistent-reply.py \
 build/preparation-firmware/springboard_rom_msc.elf build/experimental-host/libpersistent_cosim.so \
 --output build/evidence/candidate-arm-pio.json
```

Read-only timing probe libraries were built with the same flags/dependency
libraries; baseline uses the frozen C source (the owner PC constants used by
the observation fixture are unchanged):

```sh
git show c41ec8c:bridges/rp2350-springboard/src/pio_rearm_program.c > build/baseline-host/frozen_program.c
cc -shared -fPIC -O2 -fshort-enums -DAPIO_EMULATION=1 -DSB_PIO_REPLY_MARKER=1 \
 -I src -I .deps/apio/include -I .deps/epio/include \
 tests/pio_preparation_probe.c build/baseline-host/frozen_program.c \
 build/baseline-host/libepio.a -o build/baseline-host/libpreparation_probe.so
cc -shared -fPIC -O2 -fshort-enums -DAPIO_EMULATION=1 -DSB_PIO_REPLY_MARKER=1 \
 -I src -I .deps/apio/include -I .deps/epio/include \
 tests/pio_preparation_probe.c src/pio_rearm_program.c \
 build/experimental-host/libepio.a -o build/experimental-host/libpreparation_probe.so
uv run --offline --with unicorn --with pyelftools python scripts/test-pio-preparation.py \
 build/baseline-firmware/springboard_rom_msc.elf build/preparation-firmware/springboard_rom_msc.elf \
 build/baseline-host/libpreparation_probe.so build/experimental-host/libpreparation_probe.so \
 --output build/evidence/mapped-timing-and-guards.json
# Repeat that command with --full-suite and --output build/evidence/mapped-full.json.
uv run --offline --with unicorn --with pyelftools python scripts/verify-pio-preparation.py \
 build/baseline-firmware build/preparation-firmware \
 /home/mozzwald/nio/handspring-visor-plan/evidence/rom-management/2026-10-27-msc-002/recovery.rom \
 --output build/evidence/firmware-verification.json
uv run --offline --with unicorn --with pyelftools python scripts/test-boot-autoarm.py \
 build/preparation-firmware/springboard_rom_msc.elf \
 /home/mozzwald/nio/handspring-visor-plan/evidence/rom-management/2026-10-27-msc-002/recovery.rom \
 --output build/evidence/candidate-boot.json
uv run --offline --with unicorn --with pyelftools python scripts/test-msc-usb-startup.py \
 build/preparation-firmware/springboard_rom_msc.elf --output build/evidence/candidate-usb.json
uv run --offline --with unicorn --with pyelftools python scripts/test-msc-arm.py \
 build/preparation-firmware/springboard_rom_msc.elf --output build/evidence/candidate-storage-callbacks.json
```

The strict frozen MSC verifier is not weakened to accept changed PIO. A new,
specific verifier executes both actual initializers, requires exact owner and
PIO0/GPIO/main configuration equality, allows only the enumerated watcher wrap/
entry changes, checks exact new guard opcodes and all29 instruction writes, and
independently checks the container and ROM. Absolute linker symbols use nm;
the existing Palm ELF helper omits SHN_ABS. Boot/USB/storage checks use their
unchanged existing scripts.

## F. Timing comparison

**Modeled only**, using the unchanged kernel and two original timing profiles,
68-clock read windows,6/7/8-clock gaps and eight starting phases. All listed
ranges are identical before/after, including each individual matched event.
Request denotes actual PIO publication, not just CPU FIFO consumption.

| Interval, model clocks | Baseline profile1 | Candidate profile1 | Baseline profile2 | Candidate profile2 |
|---|---:|---:|---:|---:|
|CS0 select → request|12–14|12–14|12–14|12–14|
|request → ARM TX|14–20|14–20|21–38|21–38|
|TX → preload|1|1|1|1|
|preload → enable|5|5|5|5|
|CS0 release → observed Hi-Z|5|5|5|5|
|first release → next request, including tested gap|19–22|19–22|19–22|19–22|

No common owner instruction or five-slot settlement delay was changed. The
per-request CPU instructions/lookup are unchanged, without a general dispatch.
Watcher rearm is shorter: no IRQ-low wait/NOP; its next low edge arms its next
high ending. Idle OE/CS0 can now assert internal cancellation while unselected;
they clamp only, and this produces no CPU work. RESET held-low reiterates in
PIO. These internal-state differences are not hidden by the equal normal traces.

Clock150MHz and Prism default330ns are unchanged. Synthetic clock costs and
sampled inputs do not establish the Cortex-M33 worst-case, pad propagation,
Prism sampling instant or numerical setup/hold margin.

## G. Remaining concerns, artifacts and hardware gate

The final ARM kernel bytes and Thumb address remain identical at20002D35
(code starts20002D34), SHA256
`d91bc6f67770e23f0170422e4b2123b89c645a48592d823106eef0e5c8acd425`.
ROM table remains65536bytes, but moves from2000C518 to2000C508; modulo16=8
is preserved. No lookup instruction or value changes. Actual SRAM arbitration
and first-service readiness still require physical testing.

A C-template padding control was tried to prevent the16-byte shift. It instead
expanded/relocated initialization and the kernel, so was rejected. Its unflashed
artifacts/results remain under `build/padded-control` and
`build/evidence-padded-control`. The final build restores the simpler29-word
source and reproduces the original candidate hash exactly. No discarded failing
safety assertion or hidden clock adjustment is involved.

Direct clamp and sticky cancellation are preserved in source and tested phases.
No universal analog guarantee for arbitrary glitches is claimed. Physical
release/setup/hold and full lifecycle compatibility remain unqualified. The
success here is a **pre-hardware** result, not a replacement proven baseline.

Candidate files (only these final artifacts are proposed for testing):

```text
/home/mozzwald/nio/pio-dual-cs-preparation/bridges/rp2350-springboard/build/artifacts/pio-prep-exp1.uf2
/home/mozzwald/nio/pio-dual-cs-preparation/bridges/rp2350-springboard/build/artifacts/pio-prep-exp1.elf
/home/mozzwald/nio/pio-dual-cs-preparation/bridges/rp2350-springboard/build/artifacts/pio-prep-exp1.bin
/home/mozzwald/nio/pio-dual-cs-preparation/bridges/rp2350-springboard/build/artifacts/BUILD_ID.json
/home/mozzwald/nio/pio-dual-cs-preparation/bridges/rp2350-springboard/build/artifacts/rollback-msc003.uf2
```

UF2 SHA256: `fa36ced9c3cf2ad893f7cf390b95e4410c4a349799c090e0d355d7fc185b65d9`.
ELF SHA256: `b001c82f423c180c0a6b190656919d17cf9fd132696633bcb949c2dff8ca9512`.
BIN SHA256: `b1bb42f2b8d5eb8c902acbe987b002408506d518ab2a602a919a267ed2824180`.
Rollback SHA256: `3005edefaf45cbf3ee180dd464f0cc3bcf936c7c6e54ae25a6963ee4b720167b`.

The recovery ROM is unchanged6758bytes, CRC838F2520, SHA256
`081548bca3f4cbd70899b5296bebfe08c4f0a4a96d051ea67fa148821d63f27a`.
The validated stored ROM still takes priority under the existing loader rules;
no stored image was read or changed this task. Do not recopy a ROM to test this
PIO change. Firmware identity is filename/hash/build manifest: CDC product was
intentionally not changed and cannot attest the experimental build.

### Minimal subsequent physical procedure — pending authorization

Codex manages PC-side status/logging and existing serial Palm result collection;
the user performs only the cued physical/button actions. No tools are waiting
and no bench action is requested by this report.

1. Record current served-ROM identity/status and rollback availability before
   handling anything. Keep the same ROM, clock,330ns setting and wiring.
2. After explicit authorization, guide the user through the established safe
   RP2350 BOOTSEL/flash procedure. Never require holding Prism reset. Use only
   `pio-prep-exp1.uf2`; do not overwrite/copy the ROM file.
3. Verify USB enumeration and served-ROM identity/active/no-fault status on the
   PC. Initiate no automatic Prism reset or insertion.
4. Cue one separately confirmed fresh discovery using the established handling
   procedure. A soft reset is not guaranteed to discover changed databases;
   this experiment changes no database. Observe recognition and installation.
5. Launch the existing installed module application. For the present Setup/RAM
   payload, verify fresh install/idle RAM launch and its location, without
   tapping CS1 **Run four**. If the existing served payload is the historical
   ROM-only app, verify its displayed ROM execution address. Do not change ROM
   payloads solely to force that test in this experiment.
6. Use the existing bounded paired-read and complete readback/CRC workflow,
   where available, to close consecutive-read/data-integrity gates. All such
   physical results remain UNTESTED now. Defer scope unless a specific problem
   requires it. No repeated handling/wait-state sweep is planned.
7. Stop on a crash/recognition/read regression. Restore `rollback-msc003.uf2`
   through the established manual BOOTSEL procedure, preserving stored ROM.
   User-guided module removal/recovery is available if PalmOS becomes unstable;
   do not issue automated resets or hard-reset/erase instructions.

GO is for that controlled validation only. Keep archived msc-003/October25
firmware and evidence as the qualified fallback until these hardware gates pass.

## H. Next experiment

After Experiment1 is accepted on hardware, Experiment2 can investigate placing
an off-CS0 second-comparison path in the genuine free words29–31. That is room
for three instructions, not a claim that complete dual-CS admission, restoring
Y10, selected-region identity and cancellation routing are solved. Main owner
postqualification also uses Y; current CS1 assertion still cancels through
PIO0. Both issues must be addressed explicitly by the next small experiment.

No such branch or CS1 response is implemented here. The present firmware still
accepts only nibbleA and returns only the original ROM data.
