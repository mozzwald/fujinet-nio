# Host-only bank/token experiments (p4m-012)

Never flash these ELFs. No firmware target or production source is modified.

Experiment A uses a contiguous logical space: cached CS1 bank0 at20030000,
ROM bank1 at20040000. The captured control/reset code minus20000009 yields0
for CS1 and1 for CS0; every other value is rejected. The bank never comes from
live SIO. Upper-address validity, sticky cancellation and ordered completions
remain checked. The lookup adds a bank bit to the native word index.

`run_probe.py` is the unchanged268-case schedule and assertion matrix; only the
harness's SRAM address/initial logical base are adapted. AST identity of cases()
and main() is archived. The PIO fixture is byte-identical to p4m-011.
Result:268/268 PASS, but CS0 RX-to-TX15/21 versus baseline10/14 fails the required
performance gate. Read-only schedules disable Core0 polling as before.

`check_upper.py`: exhaustive bank/select/reset algebra plus32 actual compiled
upper-address cases. No out-of-range lookup may alias either table bank.
`publication_observer.c`: host-only timestamps for actual modeled PIO request
PUSH; no emulator/MCU state mutation. It leaves prior results/timings identical.

Experiment B `token_capture.c` is ONLY an isolated input-only PIO primitive:
IN_BASE1/IN16 captures A1-A15 plus CS0 as a direct16-bit word-index/bank. It also
checks raw20-bit capture, single publication and no live resampling/outputs.
160 checks PASS. Upper address and reset are demonstrably absent; using the raw
index as a qualified request would be unsafe. Host triggers stand in for the
not-yet-budgeted integrated rendezvous, not free hardware. It cannot honestly
run the full268-case bus matrix without a further integrated reallocation.

See private-plan docs/visor/UNIFIED_BUS_BANK_TOKEN_EXPERIMENTS.md for exact
budgets, timing, remaining blockers and reproduction. No design is selected
for firmware. Existing firmware, ROM and all earlier evidence are untouched.
