# CS0-first read worker — blocked host experiment

No firmware target links this. Never flash either analysis ELF. Current passive
firmware and Module B ROM are unchanged. Scope: isolate ARM scheduling by using
the existing unified-bus PIO fixture unchanged, not a proposed firmware base.

`workers.S` retains all288 bytes of the original function body except three
literal-load displacement bytes and one off-path BNE displacement byte. A new
42-byte CS1 handler is appended. It validates captured CS1 identity/cancellation
and reads only the local cached table. No CPU command processing/idle worker.
No PIO change or guard removal; normal ROM RX→TX is10/14 modeled clocks.

The rejected-ROM-select CS1 branch is too slow:21/30 RX→TX modeled clocks. On the
unchanged268-case matrix the final result is177 PASS,91 FAIL:72 slower-profile
read timing/enable failures and19 invalid-lookup safety assertions. A live GPIO
sample from an expired transaction is looked up before later qualification can
reject it. This is not proof of actual stale bus driving; PIO cancellation is
independent. It is a blocker under the existing pre-lookup ownership assertion.

## Simulator issue and independent controls

The first memory-hook run gave3/268 and is quarantined, not a firmware result.
`test_register_semantics.py` reproduces a Unicorn2.1.4 error with ordinary mapped
SRAM READ+WRITE hooks: code returns after47 instructions with stop0, whereas the
plain SRAM reference waits until explicitly stopped and returns after318 with
stop1. Empty WRITE hooks suffice; the instruction stream is unchanged.

`instruction_harness.py` is an isolated experimental backend: observe load/store
at UC_HOOK_CODE after the original instruction-cost advance, then use ordinary
mapped memory. No MEM_READ/MEM_WRITE hooks. Keep original FIFO, lookup, epoch,
output, deadline and completion assertions. Executed loads/stores are parsed
strictly, unsupported addressing asserts rather than silently skipping it.
This does not alter the compiled code or synthetic cost definitions.

The original unified worker remains237/268 and Bank A remains268/268 under this
backend. A recorded normal ROM result equals the prior backend exactly. Matrix
cases/main ASTs are unchanged. Neither positive control grants physical timing
qualification; Bank A still has its recorded physical installation failure.

Do not silently promote this experimental backend to all old tests or erase old
results. It needs further instruction/fixture coverage before production use.

## Reproduce

1. `bash build_host.sh /absolute/new/output`
2. Run `test_register_semantics.py` on cs0-first-HOST-ONLY.elf with --output NEW.
3. Run `run_probe.py ELF LIB --output NEW` for the unchanged268-case matrix.
   Existing unified workers-audit.elf is the237-case control.
4. `inspect_invalid.py ELF LIB --output NEW` records a stalled-read rejection.
5. Bank control: build experiments/unified-bank/build_host.sh separately, then
   `run_bank_control.py bank-a-HOST-ONLY.elf libbank-fixture.so --output NEW`.

Use cached uv unicorn/pyelftools as in the project. No USB/serial/Prism access.
Full evidence and decision: plan docs/visor/CS0_FIRST_READ_ROADBLOCK.md, p4m-016.
