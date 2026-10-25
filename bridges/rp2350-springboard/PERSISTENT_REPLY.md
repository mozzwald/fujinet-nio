# Persistent Core1 ROM reply experiment

Separate version3 firmware uses the existing PIO-rearm program with the preload
marker and a persistent SRAM ARM service loop. Build only:

```sh
bash scripts/build-persistent-reply.sh ABS_ROM ABS_ROM_MANIFEST ABS_OUTPUT
```

The October25 preload-marker version2 image remains the physical fallback.
The experimental target is `springboard_rom_persistent`; ordinary rearm and
marker build helpers explicitly disable this option. Version3 boots disarmed
and requires explicit `b` arm. It retains150MHz, the same GPIO configuration,
ROM, PIO guards and release paths. No per-access console calls are introduced.
The diagnostic build includes16 unreachable padding bytes to retain the
October25 ROM byte buffer and native word-table addresses; verify these addresses
against the baseline for each build rather than assuming future toolchain/ROM
changes preserve them.

## Critical path and ownership

The baseline valid ready-FIFO path executes35 instructions from completion pop
to next TX, and59 between the first two TX posts. The experiment executes22
and32, respectively. These are functional machine-code instruction counts,
not measured Cortex-M33 cycles. Register saves/restores, bus-pointer and mask
setup happen once per arm. The request pop and SIO low-address load remain
adjacent; the SRAM index, high-address FFFF rule and saved-control validation
retain their original behavior.

RX remains strictly request, completion, request, completion. Each request gets
exactly one reply. An invalid snapshot forces cancellation and posts one dummy
to release the original owner's PULL. Every completion is checked for0 or
FFFFFFFF before another request is consumed. No per-read FIFO flush, IRQ clear,
PC rewrite or owner restart is performed by the CPU.

A single consumed completion is encoded in R12 until after the next TX, or
until exit if no next request arrives. It is counted before a subsequent
completion can be consumed. LR holds requests. Final posted=requests-invalid;
completed=attempted+cancelled. The C-private totals are not readable status while
service runs. On stop, pending outcomes are flushed and exact totals published.
Invalid token framing faults the responder; it cannot be rearmed normally.
Core0's existing persistent OE clamp handles host stop, and PIO guards release
independently of a stalled CPU. The ARM loop does not manipulate data directions.

Keeping the same live low-address capture means a late CPU sample can still
observe a later bus address. Safety relies on the unchanged PIO owner/cancellation
protocol rejecting the old owner's reply. The optimization advances sampling;
it does not make that split capture atomic or certify physical setup/hold.

## Validation and measurement limits

```sh
cmake -S . -B build/host-Debug -DBRIDGE_MODE=host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host-Debug --parallel 4
ctest --test-dir build/host-Debug --output-on-failure
uv run --with unicorn --with pyelftools python scripts/test-persistent-reply.py \
  ABS_OUTPUT/springboard_rom_persistent.elf \
  build/host-Debug/libpersistent_cosim.so --output /tmp/persistent-cosim.json
```

The test executes the linked ARM instructions against the native PIO program,
including two input-sampling stages, contrasting/repeated/high addresses,
request/completion ordering, exact TX words, cancellation, CPU stalls, held
reset, invalid snapshots, malformed tokens, idle stop, deferred outcome flush
and100 consecutive requests. It retains data preloading/ownership/release
assertions. Three synthetic instruction-cost profiles explore phase sensitivity;
none is a measured M33 timing model. Profiles1/2 require independent enables
inside both physical selected intervals. The slower profile3 can command a late
second enable outside selection while still reporting two attempted outcomes;
this explicitly prevents interpreting counters as proof of valid data timing.

MMIO callbacks return register values directly. Do not implement peripherals
by writing backing memory from a Unicorn read hook: the observed translated-block
restart can retain IT state and skip a subsequent instruction. An isolated test
reproduced this harness fault; no firmware safety check was removed to avoid it.

Physical gate: use exactly four FEEDBEEF MOVE.L pairs and the existing Palm app,
scope CH1 GPIO39/CH2 CS0 and serial palm-sync collector, with explicit host flags
`--engine persistent --marker preload`. Require correct halves,8 requests/posts/
completions/enable outcomes,0 cancels, and fresh scope evidence that the second
preload advances relative to CS0 release. A marker is not bus-data validity,
Hi-Z or the Prism sampling instant. No ROM recognition is included.
