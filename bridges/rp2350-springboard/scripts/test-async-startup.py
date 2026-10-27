#!/usr/bin/env python3
"""Regress unselected bus strobes and reset recovery with real ARM/PIO. No hardware."""
import argparse,importlib.util,json,gzip
from pathlib import Path
p=Path(__file__).with_name('test-async-stage-a.py');spec=importlib.util.spec_from_file_location('stage',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def main():
 a=argparse.ArgumentParser();a.add_argument('elf',type=Path);a.add_argument('library',type=Path);a.add_argument('--output',type=Path,required=True);x=a.parse_args();runner=m.Runner(x.elf,x.library,'shadow');rows={};fails=[]
 cases={
  'unselected-read':[(100,164,0,m.RESET|(11<<16),0),(300,364,8,m.READ0,0)],
  'unselected-write':[(100,164,0,m.RESET|(7<<16),0),(300,364,8,m.READ0,0)],
  'reset-then-rom':[(100,164,0,15<<16,0),(300,364,8,m.READ0,0)],
 }
 for phase in range(8):
  for width in (1,2,4,8,16):
   t=100+phase;cases[f'unselected-overlap-{phase}-{width}']=[(t,t+width,0,m.RESET|(3<<16),0),(300,364,8,m.READ0,0)]
 cases['reset-stalled-old-owner']=[(100,164,8,m.READ0,0),(164,190,0,15<<16,0),(196,260,10,m.READ0,0),(500,564,8,m.READ0,0)]
 cases['reset-stalled-completion']=[(100,164,8,m.READ0,0),(164,190,0,15<<16,0),(500,564,8,m.READ0,0)]
 for name,schedule in cases.items():
  for profile in (1,2):
   stalls=((110,220),) if name=='reset-stalled-old-owner' else ((160,400),) if name=='reset-stalled-completion' else ()
   r=runner.run(schedule,profile=profile,stalls=stalls);r['raw_irq1']=runner.lib.sb_cs1_co_reg(1,0x30);r['raw_irq2']=runner.lib.sb_cs1_co_reg(2,0x30)
   r['pass']=r['enables']==1 and r['totals']['cancelled']==0 and all(w['driven_before_end'] for w in r['read_windows'])
   if name.startswith('reset-stalled'):
    r['pass']=r['read_windows'][-1]['driven_before_end'] and r['totals']['requests']==r['totals']['posted']==r['totals']['completed']
    if name=='reset-stalled-old-owner':r['pass']=r['pass'] and r['totals']['cancelled']>=1
   elif name!='reset-then-rom':r['pass']=r['pass'] and r['published_status']==1 and r['accepted']==0
   trace=x.output.parent/'traces'/f'{name}-p{profile}.json.gz';trace.parent.mkdir(parents=True,exist_ok=True)
   with gzip.open(trace,'wt') as f:json.dump(r.pop('trace'),f)
   rows[f'{name}/p{profile}']=r
   if not r['pass']:fails.append(f'{name}/p{profile}')
 x.output.parent.mkdir(parents=True,exist_ok=True);x.output.write_text(json.dumps(dict(results=rows,failures=fails),indent=2)+'\n');print('PASS',len(rows)-len(fails),'/',len(rows),'FAIL',fails);raise SystemExit(bool(fails))
if __name__=='__main__':main()
