#!/usr/bin/env python3
"""Read-engine safety across observed-gap-scale write activity. No hardware.
Capture qualification is intentionally separate: CS0/OE activity invalidates
the one-shot diagnostic window but must never break reads or bus release.
"""
import argparse
import json
from pathlib import Path
from dual_fixed_harness import m

p = argparse.ArgumentParser(); p.add_argument('elf', type=Path); p.add_argument('library', type=Path)
p.add_argument('--output', type=Path, required=True); a = p.parse_args()
r = m.Runner(a.elf, a.library); rows = []
for profile in (1, 2):
    for phase in range(8):
        for first, last in ((10, 10), (9, 9), (10, 9), (9, 10)):
            for gap in (5, 6, 7, 8):
                t = 100 + phase
                x = r.run([(t, t+68, 8, m.RESET | (first << 16)),
                           (t+68+gap, t+136+gap, 2, m.RESET | (5 << 16)),
                           (t+136+2*gap, t+204+2*gap, 10, m.RESET | (last << 16))], profile=profile)
                assert x['totals']['requests'] == x['totals']['completed'] == x['selected_enable_count'] == 2
                assert x['totals']['cancelled'] == 0
                expected = [0xfeed if first == 10 else 0xbeef, 0xbeef]
                assert [e['value'] for e in x['pio_events'] if e['kind'] == 4] == expected
                rows.append(dict(profile=profile, phase=phase, first=first, last=last, gap=gap, pass_read=True))
a.output.write_text(json.dumps(dict(result='PASS', cases=len(rows), rows=rows,
    scope='same read safety assertions around write-shaped traffic; no write commit or physical timing claim'), indent=2) + '\n')
print('PASS', len(rows), 'read/write/read scheduling and ownership cases')
