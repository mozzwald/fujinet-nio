#!/usr/bin/env python3
"""Reuse the unchanged 334 ROM schedules/assertions; project baseline owners.

The descriptor-specific activation/cancellation tests live in test_capture.c.
No hardware or firmware builds. Reference must be frozen msc-003 ELF.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
text=(root/'scripts/test-cs0-idle-isolation.py').read_text()
# Keep all cases, compiled ARM, safety assertions and exact ARM-result compare.
# Replace ONLY the old observer-specific activation tail with its new C suite.
cut=text.index(' # Positive activation:')
end=text.index(" report['result']='FAIL'",cut)
text=text[:cut]+text[end:]
text=text.replace("scope='Steady armed CS0 with CS1 inactive; no Core0 arbiter/USB contention or physical timing model'", "scope='ROM-owned PIO0 SM0/1+words0..2, all PIO1, full GPIO/IRQ and ARM state; CS1 inactive; no physical timing claim'")
exec(compile(text,str(root/'scripts/test-cs0-idle-isolation.py'),'exec'),dict(__file__=str(root/'scripts/test-cs0-idle-isolation.py'),__name__='__main__'))
