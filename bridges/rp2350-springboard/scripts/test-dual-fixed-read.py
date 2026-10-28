#!/usr/bin/env python3
"""Focused fixed-read tests built directly on the existing persistent harness.
No NIO, USB, hardware device access or earlier unified fixtures.
"""
import argparse,json,types,sys,gzip
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('elf',type=Path);p.add_argument('library',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--regression',action='store_true');a=p.parse_args()
from dual_fixed_harness import m
if a.regression:
 sys.argv=[sys.argv[0],str(a.elf),str(a.library),'--output',str(a.output)];m.main();raise SystemExit(0)
r=m.Runner(a.elf,a.library);READ1=m.RESET|(9<<16);rows=[]
a.output.mkdir(parents=True,exist_ok=False)
def run(name,schedule,profile=2,expected=None,cancel=False,**kw):
 x=r.run(schedule,profile=profile,**kw)
 if expected is not None:
  assert x['totals']['requests']==x['totals']['completed']==len(expected),(name,x['totals'])
  assert x['selected_enable_count']==len(expected),(name,x['totals'])
  assert x['totals']['cancelled']==0,(name,x['totals'])
  assert [v['value'] for v in x['pio_events'] if v['kind']==4]==expected,name
 if cancel:assert x['totals']['cancelled']>=1 and x['selected_enable_count']>=1,(name,x['totals'])
 trace=x.pop('trace')
 if cancel:
  with gzip.open(a.output/(name+'.trace.gz'),'wt') as f:json.dump(trace,f)
 rows.append(dict(name=name,result=x))
for profile in (1,2):
 for phase in range(8):
  for gap in (5,6,7):
   for label,first,second in [('rom-rom',m.READ,m.READ),('cs1-cs1',READ1,READ1),('rom-cs1',m.READ,READ1),('cs1-rom',READ1,m.READ)]:
    t=100+phase;second_address=0x444 if label=='cs1-rom' else 10;run(f'{label}-p{profile}-{phase}-{gap}',[(t,t+68,8,first),(t+68+gap,t+136+gap,second_address,second)],profile,[0xfeed if first==m.READ else 0xbeef,0xa174 if label=='cs1-rom' else 0xbeef])
 for control in (m.READ,READ1):
  seq=[(100+i*78,168+i*78,8 if i%2==0 else 10,control) for i in range(100)]
  run(f'consecutive-{control&0xfffff}-p{profile}',seq,profile,[m.word(x[2]) if control==m.READ else 0xbeef for x in seq])
# Deselect while OE stays low, as already covered by baseline CS0 tests.
for phase in range(8):
 for gap in (5,6,7):
  t=100+phase
  run(f'held-OE-cs1-{phase}-{gap}',[(t,t+68,8,READ1),(t+68,t+68+gap,8,m.RESET|(11<<16)),(t+68+gap,t+136+gap,10,READ1)],2,[0xbeef,0xbeef])
 for label,first,second in [('rom-cs1',m.READ,READ1),('cs1-rom',READ1,m.READ)]:
  t=100+phase;address=0x444 if second==m.READ else 10
  run(f'held-OE-{label}-{phase}',[(t,t+68,8,first),(t+68,t+74,8,m.RESET|(11<<16)),(t+74,t+142,address,second)],2,[0xfeed if first==m.READ else 0xbeef,0xa174 if second==m.READ else 0xbeef])
# Every malformed select shape remains Hi-Z; a subsequent valid ROM word proves recovery.
for reset in (0,1):
 for nibble in range(16):
  if reset and nibble in (9,10):continue
  x=r.run([(100,180,8,(m.RESET if reset else 0)|(nibble<<16)),(500,620,8,m.READ)],profile=2)
  assert x['selected_enable_count']==1 and [e['value'] for e in x['pio_events'] if e['kind']==4]==[0xfeed]
  x.pop('trace');rows.append(dict(name=f'invalid-{reset}-{nibble}',result=x))
for control in (m.READ,READ1):
 for ending in (m.IDLE,control|(1<<18),control&~(1<<19),control&~m.RESET, m.RESET|(8<<16),control|(1<<(16 if control==m.READ else 17))):
  for phase in (12,28,60,80):
   run(f'ending-{control&0xfffff}-{ending}-{phase}',[(100,100+phase,8,control),(100+phase,200+phase,8,ending),(600,760,8,m.READ)],cancel=phase==28)
 for stall in (110,140,170):
  run(f'late-{control&0xfffff}-{stall}',[(100,168,8,control),(174,242,10,m.READ if control==READ1 else READ1),(700,850,8,m.READ)],stall=(stall,500),cancel=stall<168)
(a.output/'results.json').write_text(json.dumps(dict(result='PASS',cases=len(rows),rows=rows,scope='actual compiled ARM + strict persistent PIO model; synthetic instruction costs, no hardware timing claim'),indent=2)+'\n')
print('PASS',len(rows),'fixed-read/control/transition/cancellation cases')
