#!/usr/bin/env python3
"""Host-only unified-bus feasibility; subprocesses preserve hard PIO failures."""
import argparse,importlib.util,json,subprocess,sys,gzip
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
# Same matrix/assertions, ordinary mapped-register backend. Only SRAM layout
# and initial logical lookup base differ; no FIFO or GPIO emulation changes.
import types
m=types.ModuleType('bank_harness')
harness=(ROOT/'scripts/test-async-stage-a.py').read_text()
harness=harness.replace('TABLE=0x20060000','TABLE=0x20030000')
harness=harness.replace('(UC_ARM_REG_R1, WORDS)','(UC_ARM_REG_R1, TABLE)')
exec(compile(harness,'bank_harness','exec'),m.__dict__)
def cases():
 out={}
 for phase in range(8):
  for name,kinds in [('rom',(m.READ0,m.READ0)),('regs',(m.READ1,m.READ1)),('rom-reg',(m.READ0,m.READ1)),('reg-rom',(m.READ1,m.READ0))]:
   for gap in (5,6,7):
    t=100+phase
    out[f'{name}-{phase}-{gap}']=dict(schedule=[(t,t+64,8 if kinds[0]==m.READ0 else 0,kinds[0],0),(t+64+gap,t+128+gap,10 if kinds[1]==m.READ0 else 2,kinds[1],0)],nreads=2,nwrites=0)
 for n in range(8):
  t=100+n
  out[f'write-rom-{n}']=dict(schedule=[(t,t+64,26,m.WRITE|(1<<20),0),(t+70,t+134,8,m.READ0,0),(420,484,28,m.READ1,0)],nreads=2,nwrites=1)
  out[f'write-poll-{n}']=dict(schedule=[(t,t+64,26,m.WRITE|(1<<20),0),(t+70,t+134,6,m.READ1,0),(420,484,28,m.READ1,0)],nreads=2,nwrites=1)
 out['cancel-write']=dict(schedule=[(100,164,26,m.WRITE|(1<<20),0),(164,180,26,m.RESET|(7<<16),0),(420,484,8,m.READ0,0)],nreads=1,nwrites=0)
 out['held-reset']=dict(schedule=[(100,250,0,15<<16,0),(420,484,8,m.READ0,0)],nreads=1,nwrites=0)
 for t in (100,120,140,160,180):
  out[f'stall-{t}']=dict(schedule=[(100,164,8,m.READ0,0),(170,234,2,m.READ1,0),(700,800,10,m.READ0,0)],stalls=[(t,500)],cancel=True)
 out['late-reg']=dict(schedule=[(100,164,0,m.READ1,0),(170,234,10,m.READ0,0),(700,800,2,m.READ1,0)],stalls=[(120,500)],cancel=True)
 out['alternation']=dict(schedule=[(100+70*i,164+70*i,8 if i%2==0 else 0,m.READ0 if i%2==0 else m.READ1,0) for i in range(16)],nreads=16,nwrites=0)
 for select in (m.READ0,m.READ1):
  for width in (4,12,28):
   out[f'cancel-read-{select&0xfffff}-{width}']=dict(schedule=[(100,100+width,8 if select==m.READ0 else 0,select,0),(500,600,10,m.READ0,0)],cancel=True)
  # Same select, OE ends; then reset while an output could be active.
  out[f'reset-active-{select&0xfffff}']=dict(schedule=[(100,164,8 if select==m.READ0 else 0,select,0),(164,200,0,15<<16,0),(420,520,10,m.READ0,0)],nreads=2,nwrites=0)
  cs_before=select | (1<<18)
  oe_before=m.RESET | (11<<16)
  for label,ctrl in [('CS-before-OE',cs_before),('OE-before-CS',oe_before)]:
   out[f'{label}-{select&0xfffff}']=dict(schedule=[(80,100,0,ctrl,0),(100,164,8 if select==m.READ0 else 0,select,0),(500,600,10,m.READ0,0)],nreads=2,nwrites=0)
 out['overflow']=dict(schedule=[(100+i*80,164+i*80,26,m.WRITE|(1<<20),0) for i in range(7)]+[(800,900,8,m.READ0,0)],core0_delay=700,fault=True)
 return out

def main():
 p=argparse.ArgumentParser();p.add_argument('elf',type=Path);p.add_argument('library',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--worker');p.add_argument('--profile',type=int);p.add_argument('--quick',action='store_true');a=p.parse_args();cs=cases()
 if a.worker:
  c=cs[a.worker].copy();schedule=c.pop('schedule');nr=c.pop('nreads',None);nw=c.pop('nwrites',0);cancel=c.pop('cancel',False);fault=c.pop('fault',False)
  if not any((row[3]>>16)&15==5 for row in schedule):c['core0_period']=0
  trace=a.output.with_suffix('.trace');r=m.Runner(a.elf,a.library).run(schedule,profile=a.profile,trace_file=trace,**c)
  r.pop('trace');a.output.write_text(json.dumps(r,indent=2)+'\n')
  assert not r['totals']['fault'],r['totals']
  # The retained persistent kernel reports posted=valid snapshots, while
  # TX events include its forced-cancel dummy. Validate both without
  # relabeling an invalid sample as a normal posted response.
  tx=[e for e in r['events'] if e['kind']=='TX']
  assert r['totals']['requests']==r['totals']['completed']==len(tx),r['totals']
  assert r['totals']['posted']==sum(not e['cancel_forced'] for e in tx),r['totals']
  if fault:
   assert r['accepted']==0 and r['published_status']&16 and r['published_error']==7,r
  else:assert r['accepted']==nw,(r['accepted'],nw)
  if fault:assert r['enables']==1 and all(w['driven_before_end'] for w in r['read_windows'])
  elif cancel:assert r['totals']['cancelled']>=1 and r['enables']>=1,r['totals']
  else:
   assert r['enables']==nr,(r['enables'],nr)
   assert all(w['driven_before_end'] for w in r['read_windows']),r['read_windows']
   assert r['totals']['cancelled']==0,r['totals']
  return
 a.output.mkdir(parents=True,exist_ok=False);report=[]
 if a.quick:cs={k:cs[k] for k in ('rom-0-6','regs-0-6','rom-reg-0-6','reg-rom-0-6','write-rom-0','write-poll-0','cancel-write','held-reset','stall-120','alternation')}
 for name in cs:
  for profile in (1,2):
   out=a.output/f'{name}-p{profile}.json'
   r=subprocess.run([sys.executable,__file__,str(a.elf),str(a.library),'--output',str(out),'--worker',name,'--profile',str(profile)],capture_output=True,text=True)
   trace=out.with_suffix('.trace')
   if trace.exists():
    with gzip.open(str(trace)+'.gz','wb') as f:f.write(trace.read_bytes())
    trace.unlink()
   report.append(dict(case=name,profile=profile,passed=r.returncode==0,returncode=r.returncode,stderr=r.stderr[-4000:]))
   (a.output/'summary.json').write_text(json.dumps(dict(backend='mapped-register',cases=report,passed=sum(x['passed'] for x in report),total=len(report)),indent=2)+'\n')
 print(sum(x['passed'] for x in report),'/',len(report));print([x for x in report if not x['passed']][:3]);raise SystemExit(any(not x['passed'] for x in report))
if __name__=='__main__':main()
