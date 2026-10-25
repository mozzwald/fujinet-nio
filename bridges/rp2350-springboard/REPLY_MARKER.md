# Separate reply-preload marker diagnostic

This is instrumentation for second-response cancellation, not a responder
redesign. The installed drive-marker image remains the initial spacing-test
control. This build is separately archived, boot-disarmed and not automatically
flashed or armed.

```
bash scripts/build-reply-marker.sh ABSOLUTE_ROM ABSOLUTE_ROM_MANIFEST ABSOLUTE_OUTPUT
```

At the unchanged150MHz/PIO divider1, the32-instruction PIO program differs only
in two optional side-set fields:

| PC | Drive-marker image | Reply-preload image |
| --- | --- | --- |
| 7: OUT PINS,16 | no side-set | GPIO39 high |
| 13: MOV PINDIRS,~STATUS | GPIO39 high | no side-set |

All control opcodes, instruction count/delays, SM registers, cancellation guards,
FIFO ownership, ARM source and ROM remain unchanged. The128-byte ARM response
kernel must be byte-identical. The q banner explicitly says `marker=preload`;
tools require `--marker preload`. Normal builds explicitly set the diagnostic
option OFF, preventing accidental reuse from a cached diagnostic build.

GPIO39 high at PC7 says the old-owned TX word has been pulled at PC6 and loaded
into the output latch while Hi-Z. It does **not** mean data pins are enabled.
Guards and PC19 still lower the marker; their existing independent release
behavior is preserved. No CPU instruction or UART output is added per read.

A preload pulse first occurring after the physical read ends is positive
evidence that PIO consumed/loaded the response too late, subject to channel skew
and threshold qualification. A preload pulse inside CS0 with a cancelled token
requires checking the six-clock qualification/enable tail, OE/reset qualification
and cancellation ownership before blaming the lookup. Compare the same pattern,
NOP count and physical timing, changing only the marker firmware.

A missing pulse is inconclusive: a guard can overwrite side-set in the same
clock, and short pulses can be below scope resolution. A token0 also combines
invalid live qualification, pre-enable cancellation and cancellation in the
clock after enable. Markers and final totals do not provide per-word TX values
or distinguish every one of those paths. Keep these limits in the evidence.
