#!/usr/bin/env python3
"""Matched-cost, matched-schedule comparison; synthetic clocks only."""
import argparse,json,gzip
from pathlib import Path
from dual_fixed_harness import m
p=argparse.ArgumentParser();p.add_argument('baseline_elf',type=Path);p.add_argument('candidate_elf',type=Path);p.add_argument('baseline_library',type=Path);p.add_argument('candidate_library',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
runners=[m.Runner(a.baseline_elf,a.baseline_library),m.Runner(a.candidate_elf,a.candidate_library)];rows=[]
for profile in (1,2):
 for phase in range(8):
  t=100+phase
  for engine in ('exp1-cs0','exp2-cs0','exp2-cs1'):
   selected=m.READ if engine!='exp2-cs1' else m.RESET|(9<<16)
   r=runners[0 if engine=='exp1-cs0' else 1].run([(t,t+68,8,selected),(t+74,t+142,10,selected)],profile=profile)
   assert r['totals']['attempted']==2 and r['selected_enable_count']==2 and not r['totals']['cancelled']
   trace=r.pop('trace')
   with gzip.open(a.output/f'{engine}-{profile}-{phase}.trace.gz','wt') as f:json.dump(trace,f)
   events=r['pio_events'];pub=[e['clock']+1 for e in events if e['kind']==1];en=[e['clock']+1 for e in events if e['kind']==4];pre=[e['clock']+1 for e in events if e['kind']==5 and e['value']==1];rel=[e['clock'] for e in events if e['kind']==7]
   tx=[e['clock'] for e in r['events'] if e['event']=='TX'];rx=[e['clock'] for e in r['events'] if e['event']=='request']
   rows.append(dict(engine=engine,profile=profile,phase=phase,select=[t,t+74],end=[t+68,t+142],request=pub,rx=rx,tx=tx,preload=pre,enable=en,release=rel,totals=r['totals']))
metrics={}
for engine in ('exp1-cs0','exp2-cs0','exp2-cs1'):
 for profile in (1,2):
  values={n:[] for n in ('select_request','request_tx','rx_tx','tx_preload','preload_enable','select_enable','end_release')}
  for r in rows:
   if r['engine']!=engine or r['profile']!=profile:continue
   for key,aa,bb in [('select_request','request','select'),('request_tx','tx','request'),('rx_tx','tx','rx'),('tx_preload','preload','tx'),('preload_enable','enable','preload'),('select_enable','enable','select'),('end_release','release','end')]:values[key]+=[x-y for x,y in zip(r[aa],r[bb])]
  metrics[engine+'-p'+str(profile)]={k:[min(v),max(v)] for k,v in values.items()}
(a.output/'results.json').write_text(json.dumps(dict(scope='equal original uncalibrated ARM profiles; 68-clock windows,6-clock gaps,8 phases; no physical sampling margin',metrics=metrics,rows=rows),indent=2)+'\n');print(json.dumps(metrics,indent=2))
