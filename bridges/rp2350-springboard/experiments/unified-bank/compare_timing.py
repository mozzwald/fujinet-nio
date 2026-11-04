#!/usr/bin/env python3
"""Equal synthetic costs, frozen CS0 versus unified host-only read engine."""
import importlib.util,json,sys,gzip
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];WORK=ROOT.parents[2]
s=(ROOT/'scripts/test-persistent-reply.py').read_text();a=s.index('            cost = 1\n');b=s.index('            # Count condition-failed',a)
s=s[:a]+'''            cost = 1 + (profile - 1 if mnemonic.startswith(('ldr','str','beq','bne','bhi','cbz','cbnz','bcc','bcs','tbb','tbh')) else 0)
            if mnemonic.startswith(('push','pop')) or ('sp!' in op and mnemonic.startswith(('stmdb','ldmia'))):cost+=8
'''+s[b:]
ns={'__name__':'baseline_mapped','__file__':str(ROOT/'scripts/test-persistent-reply.py')};exec(compile(s,'baseline_mapped','exec'),ns)
spec=importlib.util.spec_from_file_location('bank_probe',ROOT/'experiments/unified-bank/run_probe.py');bank=importlib.util.module_from_spec(spec);spec.loader.exec_module(bank);m=bank.m
p=ns['Runner'](WORK/'handspring-visor-plan/evidence/rom-management/2026-10-27-msc-003/rom-drive.elf',ROOT/'build/phase4-Release/libpersistent_cosim.so')
u=m.Runner(Path(sys.argv[1]),Path(sys.argv[2]));out=Path(sys.argv[3]);out.mkdir(exist_ok=False);rows=[]
for profile in (1,2):
 for phase in range(8):
  t=100+phase;seq=[(t,t+64,8,m.READ0),(t+70,t+134,10,m.READ0)]
  for engine in ('msc-003','unified-probe'):
   if engine=='msc-003':
    x=p.run(seq,profile=profile);tx=[e['clock'] for e in x['events'] if e['event']=='TX'];rx=[e['clock'] for e in x['events'] if e['event']=='request'];en=[e['clock']+1 for e in x['pio_events'] if e['kind']==4]
   else:
    x=u.run([(*v,0) for v in seq],profile=profile,core0_period=0);tx=[e['clock'] for e in x['events'] if e['kind']=='TX'];rx=[e['clock'] for e in x['events'] if e['kind']==1];en=[e['clock'] for e in x['events'] if e['kind']=='enable']
   published=([e['clock']+1 for e in x['pio_events'] if e['kind']==1] if engine=='msc-003' else [u.lib.sb_bank_publication_clock(e['owner']) for e in x['events'] if e['kind']==1])
   trace=x.pop('trace')
   with gzip.open(out/f'{engine}-p{profile}-{phase}.trace.json.gz','wt') as f:json.dump(trace,f)
   rows.append(dict(engine=engine,profile=profile,phase=phase,start=t,rx=rx,tx=tx,enable=en,published=published,result=x))
(out/'results.json').write_text(json.dumps(dict(scope='Equal uncalibrated ARM costs; 64-clock windows/6-clock gaps, not physical sampling deadlines',rows=rows),indent=2)+'\n')
for engine in ('msc-003','unified-probe'):
 for profile in (1,2):
  rs=[r for r in rows if r['engine']==engine and r['profile']==profile]
  print(engine,profile,'firstTX',[r['tx'][0]-r['start'] for r in rs],'firstEnable',[r['enable'][0]-r['start'] for r in rs])
