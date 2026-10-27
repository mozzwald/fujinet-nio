# Experimental asynchronous Springboard Stage A

This separate candidate provides fixed CS1 identification/status reads and a
bounded PROBE/ACK token diagnostic. It is not a completed NIO mailbox or a
hardware-qualified replacement for the preserved ROM-only responder. Legacy
synchronous cs1_reply.S experiments and scheduling cases remain as reference;
the selected async_reply.S does not drain write FIFOs in its read critical path.

Core1/PIO service CS0/CS1 reads through a single output owner. Core0 retires paired
write snapshots only after validated epoch/outcome, publishes aligned table
halfwords, and never waits for a backend in a live bus transaction. Cancelled
writes have no effect, stale read ownership is rejected before lookup/reply,
and autonomous PIO guards release outputs. Sticky fault history does not itself
cancel later eligible ROM reads; reset still quarantines writes until coordinated
stop/rearm. Boot autoarm is one-shot, not automatic fault recovery.

Default CMake options remain off. Separate builds use scripts/build-cs1-module.sh
with absolute verified ROM, manifest and fresh output directory. They never flash
or reset hardware. Keep150MHz, GPIO/drive and Prism330ns unchanged. q/i/s/g/r/o
are bounded debug records; no per-access UART. Read totals are final only after
controlled stop; do not infer live zero totals mean no transactions occurred.

Host Debug/Release12 tests each,1478 compiled mapped-register ARM/PIO cases and90
startup cases passed at this checkpoint. Both instruction-cost profiles are
models, not measured M33 margin. Core0 is native C at a modeled retirement cadence,
not a second simultaneously executed ARM core. Legacy sustained scheduling
failures remain archived, not silently removed. Physical CS1 data/release/timing
qualification remains open: the first RAM diagnostic stopped in a lifecycle
guard, and the updated diagnostic has not yet installed on the current bench.

The frozen October25 and Phase2 ROM engine sources are unchanged. Private bench
artifacts preserve failed/successful runs and exact UF2 provenance separately.
