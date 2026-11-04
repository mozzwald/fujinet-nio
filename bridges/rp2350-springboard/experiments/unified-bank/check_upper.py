#!/usr/bin/env python3
"""Supplemental captured-bank algebra and actual upper-address regressions."""
import importlib.util,json,sys,gzip
from pathlib import Path
spec=importlib.util.spec_from_file_location('bank',Path(__file__).with_name('run_probe.py'));bank=importlib.util.module_from_spec(spec);spec.loader.exec_module(bank);m=bank.m
out=Path(sys.argv[3]);out.mkdir(exist_ok=False);rows=[]
# Exhaustive select/reset qualification and bank mapping, independent of live GPIO.
for reset in (0,1):
 for nibble in range(16):
  capture=(reset<<29)|nibble
  index=((capture&0x2000000f)-0x20000009)&0xffffffff
  assert (index<=1)==(reset==1 and nibble in (9,10))
  if index<=1:
   for low in range(0,65536,2):
    address=((low>>1)|(index<<15))*2
    assert address==low+(65536 if nibble==10 else 0)
for profile in (1,2):
 for select in (m.READ0,m.READ1):
  for bit in range(16,24):
   addr=(1<<bit)|8
   schedule=[(100,180,addr,select,0),(300,380,8,m.READ0,0),(500,580,0,m.READ1,0)]
   r=m.Runner(Path(sys.argv[1]),Path(sys.argv[2])).run(schedule,profile=profile,core0_period=0)
   tx=[e for e in r['events'] if e['kind']=='TX'];assert [v['word'] for v in tx]==[0xffff,0xfeed,0x464e]
   owner=next(e['owner'] for e in r['events'] if e['kind']==1)
   assert not any(e['kind']=='lookup' and e['owner']==owner for e in r['events']),'upper address aliased into table'
   assert all(w['driven_before_end'] for w in r['read_windows'])
   with gzip.open(out/f'upper-{profile}-{select&15}-{bit}.trace.gz','wt') as f:json.dump(r.pop('trace'),f)
   rows.append(dict(profile=profile,select=select,upper_bit=bit,result=r))
(out/'results.json').write_text(json.dumps(dict(bank_algebra='all reset/control combinations and all aligned low indices PASS',upper_cases=rows,passed=len(rows)),indent=2)+'\n');print('PASS bank algebra and',len(rows),'compiled upper-address cases')
