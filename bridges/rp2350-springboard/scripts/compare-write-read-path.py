#!/usr/bin/env python3
"""Matched original read scheduling; modeled clocks, never physical margin."""
import argparse
import json
from pathlib import Path
from dual_fixed_harness import m

p = argparse.ArgumentParser()
for n in ('baseline_elf', 'candidate_elf', 'baseline_library', 'candidate_library'): p.add_argument(n, type=Path)
p.add_argument('--output', type=Path, required=True); a = p.parse_args()
old = m.Runner(a.baseline_elf, a.baseline_library)
new = m.Runner(a.candidate_elf, a.candidate_library)
rows = []
for profile in (1, 2):
    for phase in range(8):
        for regions in ((10, 10), (9, 9), (10, 9), (9, 10)):
            t = 100 + phase
            schedule = [(t, t+68, 8, m.RESET | (regions[0] << 16)),
                        (t+74, t+142, 10, m.RESET | (regions[1] << 16))]
            x, y = old.run(schedule, profile=profile), new.run(schedule, profile=profile)
            for key in ('totals', 'selected_enable_count', 'events', 'pio_events'):
                assert x[key] == y[key], (profile, phase, regions, key)
            rows.append(dict(profile=profile, phase=phase, regions=regions, event_scheduling_identical=True))
a.output.write_text(json.dumps(dict(result='PASS', cases=len(rows),
    maximum_modeled_enable_delta_clocks=0, kernel='same compiled read instructions',
    scope='matched uncalibrated instruction-cost profiles, PIO captures enabled; not physical SRAM/bus timing',
    rows=rows), indent=2) + '\n')
print('PASS', len(rows), 'matched read schedules; max modeled enable delta0')
