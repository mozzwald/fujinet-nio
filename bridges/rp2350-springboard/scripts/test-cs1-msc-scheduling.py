#!/usr/bin/env python3
"""Additional bounded Core0-delay cases over unchanged ARM/PIO; no USB device."""
import argparse,importlib.util,json,gzip
from pathlib import Path
spec=importlib.util.spec_from_file_location('async_test',Path(__file__).with_name('test-async-stage-a.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
p=argparse.ArgumentParser(description=__doc__);p.add_argument('elf',type=Path);p.add_argument('library',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
r=m.Runner(a.elf,a.library,'shadow');results=[]
for profile in (1,2):
 for delay in (1000,3000):
  schedule=[(100,164,26,m.WRITE|(1<<20),0),(300,364,28,m.READ1,0),(500,564,8,m.READ0,0),(delay+300,delay+364,28,m.READ1,0)]
  x=r.run(schedule,profile=profile,core0_delay=delay)
  assert x['accepted']==1 and x['published_status']==1
  assert all(w['driven_before_end'] for w in x['read_windows'])
  values=[]
  for e in x['events']:
   if e['kind']=='lookup' and e['address']==m.TABLE+28:
    values.append(next(t['word'] for t in x['events'] if t['kind']=='TX' and t['owner']==e['owner']))
  assert values==[0,1],values
  results.append(dict(profile=profile,core0_delay=delay,ack_reads=values,accepted=x['accepted'],pass_=True))
a.output.write_text(json.dumps(dict(result='PASS',cases=results,scope='Synthetic Core0 retirement delay; not actual USB/flash duration or dual-core arbitration'),indent=2)+'\n');print('PASS four delayed-retirement/old-ACK-to-new-ACK cases, CS0 still serviced')
