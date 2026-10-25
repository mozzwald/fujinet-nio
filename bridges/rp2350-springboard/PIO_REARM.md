# PIO-owned continuous rearm candidate

This is a separate, boot-disarmed `springboard_rom_pio_rearm` image. The old
continuous image and RX-first-v2 bounded fallback are not replaced or rebuilt.
No recognition, module insertion/reset, or ROM application launch is automatic.
Build a verified ROM with three absolute paths:

```
scripts/build-pio-rearm.sh ROM MANIFEST OUTPUT_DIRECTORY
```

The ARM `cpu_reply.S` lookup routine is unchanged. The owner passes the SRAM
stop byte instead of the cancellation IRQ byte, matching the original bounded
kernel contract. All normal per-word IRQ acknowledgments, PC rewrites, SM/divider
restarts and FIFO flushes have been removed from Core1. No DMA or new bus pins
are used. Clock150MHz, pad drive/slew/input setup and ROM image stay unchanged.

## Ownership and framing

SM0 RX FIFO strictly alternates request snapshot, completion, request snapshot,
completion. Core1 calls the unchanged kernel only for a request; after the
kernel publishes TX, the wrapper consumes exactly one completion before calling
it again. Completion is0 for a dropped request orFFFFFFFF when the conditional
enable was unmasked through the next cycle. Any other token stops service and
marks ready0. Both FIFOs are exclusively owned by Core1 and this PIO program.

SM0 must consume one TX reply before it emits completion and rearms. Thus a
late reply remains owned by the old request. If a captured snapshot is invalid,
the kernel intentionally publishes nothing; the wrapper sets cancellation and
supplies one dummy TX word so that transaction can be drained safely. It never
counts that dummy as a posted ROM reply. No normal FIFO clear is involved.
The RX backlog is at most two words (completion plus next request); TX backlog
is at most one. Startup/explicit stop alone drain FIFOs while parked and Hi-Z.

Cancellation IRQ0 stays latched while SM0 waits for its reply. At the direction
instruction, `MOV PINDIRS, ~STATUS` reads IRQ0 in that same PIO instruction:
cancelled requests cannot become enabled merely because another read is now
selected. The next instruction saves the inverted status as the completion
outcome. `WAIT 1 IRQ0` after that point consumes the ending flag; only then can
completion and new ownership proceed. This instruction automatically clears
the IRQ, unlike the old `IRQ SET WAIT` owner rendezvous.

CS0, OE and RESET watchers remain in PIO1 SM1/2/3, and independently force all16
data directions low on ending. They do not continuously clamp: the atomic IRQ
mask on every main enable is what prevents a subsequent old-reply enable.
Because the main no longer polls all controls while active, two input-only PIO0
SMs now watch WE/CS1 and set PIO1 IRQ0 through NEXT IRQ routing. Data/marker pads
remain mapped to PIO1. PIO0 writes no bus pins.

## Exact PIO1 layout

Every instruction is one enabled PIO clock, plus any explicit delay or FIFO/
WAIT stall. Dividers are1. Optional side-set controls GPIO39; an absent side-set
holds its previous value, including across a stalled PULL/WAIT.

| PC | Operation |
| --- | --- |
| 0 | MOV OSR,PINS |
| 1 | OUT X,4 |
| 2 | JMP X!=Y,0 (Y=10) |
| 3 | IRQ CLEAR0 [5] (six clocks total) |
| 4 | IN PINS,32 (request ownership snapshot) |
| 5 | PUSH BLOCK (request) |
| 6 | PULL BLOCK (one reply for this request) |
| 7 | OUT PINS,16 (preload while Hi-Z) |
| 8 | MOV OSR,PINS |
| 9 | OUT X,4 |
| 10 | JMP X!=Y,16 |
| 11 | JMP RESET-high,13 |
| 12 | JMP16 |
| 13 | MOV PINDIRS,~STATUS (IRQ0); marker high |
| 14 | MOV X,~STATUS |
| 15 | JMP18 |
| 16 | SET X,0 |
| 17 | IRQ SET0 |
| 18 | WAIT1 IRQ0 (ending acknowledgment, automatic clear) |
| 19 | MOV PINDIRS,NULL; marker low |
| 20 | MOV ISR,X |
| 21 | PUSH BLOCK (completion0/FFFFFFFF) |
| 22 | JMP0 |
| 23 | WAIT GPIO CS0-low |
| 24 | WAIT own pin-low |
| 25 | WAIT own pin-high |
| 26 | IRQ SET0 |
| 27 | MOV PINDIRS,NULL; marker low |
| 28 | WAIT0 IRQ0 |
| 29 | NOP; per-SM wrap |
| 30 | WAIT RESET-low |
| 31 | JMP26 |

SM0 wraps0..22. CS0/OE watchers start/wrap at23; RESET starts30 and wraps29→30.
PC3 lets old watcher acknowledgments settle before the new snapshot. PIO0
SM0/1 share three instructions: WAIT own-low, IRQ SET NEXT0, WAIT own-high/wrap.
Their inputs are WE(GPIO19) and CS1(GPIO17), respectively.

## Console and counters

Version2 uses the same R/Z/F,b,x,q,s commands and three bounded status pages.
A `counters_final=1` field explicitly marks final-only counter publication:
while active, totals are not a progress meter. Core1 keeps local counters and
publishes exact totals before stopped1. This avoids shared-SRAM read/modify/
write diagnostics between paired replies. Host collection waits for stopped1.
Version1 clients/firmware retain their prior behavior; current Palm tools accept
both versions and require version2 snapshot metadata.

`requests` counts snapshots popped by the kernel, `posted` counts real kernel
TX writes, `completed` counts completion tokens, `attempted` counts unmasked
conditional enables, and `cancelled` counts dropped outcomes. A cancellation
arriving in the cycle immediately after enable is conservatively a dropped
outcome. GPIO39 is a conditional enable-attempt marker; on cancelled paths a
pulse is not proof of actual output enable, valid data, Hi-Z, or CPU sampling.
Normal qualification requires payload, exact2N counters/zero drops and physical
scope evidence together. Host stop retains the persistent pad OE overrides,
sets cancellation, and parks/drains the state machines before another arm.

## Validation boundary

Native Debug/Release tests exercise2250 paired timing/CPU-delay combinations,
120 OE lead/lag/held-active cases,10080 cancellation/late-reply phases,80 CPU
stall phases,200 continuous changing/repeated addresses including0000/FFFF,
and held RESET recovery. Tests check actual directions/data before each modeled
normal read ends, FIFO framing/depth, preload-before-enable, and cancellation
masking at the direction instruction.

The emulator has no input synchronizer, analogue pads, metastability or M33 bus
contention model. On PIO-visible input edges, normal CS0/OE release is bounded
by three clocks; WE/CS1/RESET paths have their own finite propagation. The model
also records240 enable attempts in the <=2-clock interval before cancellation
is latched in adversarial edge tests. Those are not silently classified as zero-
latency edge rejection. After the cancellation flag is latched, an old reply
cannot create a new driven interval. Shorter-than-release gaps are not physical
Hi-Z qualification. GPIO39/channel skew and the Prism sampling instant remain
unqualified. These tests are a flash gate, not a hardware timing pass.

Physical sequence: four FEEDBEEF longwords, four A1745C8F longwords; inspect both
halves,2N counters, both scope responses and release; only then100 and10,000 per
pattern. Stop on any failure. The existing Palm PRC/control/session is reused;
record the installed firmware hash for every run and require smaller-run passes
from that same hash. No ROM recognition or execution until these gates pass.

## October24 physical checkpoint

The drive-marker candidate accepts/posts/completes all8 transactions of four
FEEDBEEF MOVE.L reads, but returns FEEDFEED for every pair:4 unmasked-enable
outcomes and4 cancellations. Its initial captured inactive gap is36–44ns.
The subsequent separate RAM app executes two explicit MOVE.W instructions;
NOP0 and NOP1 each pass four pairs, all8 requests/posts/completions/enable
outcomes and zero cancellations. Their initial captured gaps are536–544ns and
608–616ns respectively. Firmware, ROM, clock, pad configuration and Prism wait
setting are unchanged across those controls.

Only the initial pair of each run has precise scope timing; remaining pairs
have coarse6MHz logic timing. This supports a gap-sensitive turnaround or
ownership issue, not a CPU-only diagnosis or an exact minimum reliable gap.
Both longer CPU preparation and guard settling can benefit from the extra
spacing. The unchanged split low-address capture remains unqualified at the
failing timing. GPIO39 and successful returned values do not establish an
electrical Hi-Z or numerical setup/hold margin.

The [separate preload marker](REPLY_MARKER.md) is built, not flashed. It can
help distinguish response arrival from subsequent qualification on a repeated
failing control. At the user checkpoint the responder is stopped and no next
read is armed; recognition and full-ROM escalation remain blocked.
