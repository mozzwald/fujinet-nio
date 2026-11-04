#!/usr/bin/env python3
"""Actual read-only enable masks and immutable registers; no Core0 adapter work."""
import importlib.util,json,gzip,sys
from pathlib import Path
spec=importlib.util.spec_from_file_location('linked',Path(__file__).with_name('run_linked_matrix.py'));linked=importlib.util.module_from_spec(spec);spec.loader.exec_module(linked);m=linked.m
elf,lib,out=Path(sys.argv[1]),Path(sys.argv[2]),Path(sys.argv[3]);out.mkdir(exist_ok=False);rows=[]
cases={}
for phase in range(8):
 t=100+phase
 for a,b,name in ((m.READ0,m.READ0,'ROM'),(m.READ1,m.READ1,'registers'),(m.READ0,m.READ1,'ROM-register'),(m.READ1,m.READ0,'register-ROM')):
  cases[f'{name}-{phase}']=dict(schedule=[(t,t+64,8 if a==m.READ0 else 0,a,0),(t+70,t+134,10 if b==m.READ0 else 2,b,0)],nreads=2)
 for nextread in (m.READ0,m.READ1):
  cases[f'ignored-write-{phase}-{nextread&15}']=dict(schedule=[(t,t+64,26,m.WRITE|(1<<20),0),(t+70,t+134,8 if nextread==m.READ0 else 28,nextread,0),(420,484,6,m.READ1,0)],nreads=2)
for select in (m.READ0,m.READ1):
 cases[f'reset-{select&15}']=dict(schedule=[(100,164,8 if select==m.READ0 else 0,select,0),(164,250,0,15<<16,0),(420,484,8,m.READ0,0)],nreads=2)
 cases[f'upper-{select&15}']=dict(schedule=[(100,164,0x10008,select,0),(300,364,8,m.READ0,0)],nreads=2)
 cases[f'cancel-{select&15}']=dict(schedule=[(100,112,8 if select==m.READ0 else 0,select,0),(500,600,10,m.READ0,0)],cancel=True)
 cases[f'late-{select&15}']=dict(schedule=[(100,164,8 if select==m.READ0 else 0,select,0),(500,600,10,m.READ0,0)],stalls=[(110,400)],cancel=True)
cases['write-flood-no-capture']=dict(schedule=[(100+80*i,164+80*i,26,m.WRITE|(1<<20),0) for i in range(7)]+[(800,864,28,m.READ1,0),(900,964,8,m.READ0,0)],nreads=2)
for name,case in cases.items():
 for profile in (1,2):
  c=case.copy();schedule=c.pop('schedule');nr=c.pop('nreads',None);cancel=c.pop('cancel',False)
  runner=m.Runner(elf,lib);x=runner.run(schedule,profile=profile,core0_period=0,**c)
  assert x['accepted']==0 and x['published_status']==8 and x['published_error']==1
  assert runner.lib.sb_bank_model_write_depth()==0,'disabled capture FIFO activity'
  tx=[e for e in x['events'] if e['kind']=='TX'];assert x['totals']['requests']==x['totals']['completed']==len(tx)
  assert x['totals']['posted']==sum(not e['cancel_forced'] for e in tx)
  assert not x['totals']['fault']
  if cancel:assert x['totals']['cancelled']>=1 and x['enables']>=1
  else:assert x['enables']==nr and all(w['driven_before_end'] for w in x['read_windows'])
  with gzip.open(out/f'{name}-p{profile}.trace.gz','wt') as f:json.dump(x.pop('trace'),f)
  rows.append(dict(name=name,profile=profile,result=x))
(out/'results.json').write_text(json.dumps(dict(result='PASS',cases=rows,total=len(rows),scope='actual read-only PIO masks/immutable register table, linked ARM and real SRAM addresses; Core0 polling disabled'),indent=2)+'\n');print('PASS',len(rows),'read-only cases')
