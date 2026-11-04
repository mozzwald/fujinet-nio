#!/usr/bin/env python3
"""Local observer saturation must not change ROM state or depend on a consumer."""
import importlib.util,ctypes as C,json,sys,hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('rom',root/'scripts/test-persistent-reply.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Projection:
 def __init__(self,expected=None):self.rows=[];self.expected=expected
 def __call__(self,lib):
  x=(C.c_uint32*1024)();lib.sb_idle_snapshot.argtypes=[C.POINTER(C.c_uint32),C.c_uint];n=lib.sb_idle_snapshot(x,1024);v=bytes(x)[:n*4]
  if self.expected is not None:assert v==self.expected[len(self.rows)],'ROM state changed with observer pressure'
  self.rows.append(v)
base,candidate,reference,observer,out=map(Path,sys.argv[1:]);rows=[]
for phase in range(8):
 for profile in (1,2):
  sequence=[(80+phase+i*80,144+phase+i*80,0,m.RESET|(9<<16)) for i in range(8)]
  sequence += [(900,968,8,m.READ),(974,1042,10,m.READ)]
  a=Projection();x=m.Runner(base,reference,cycle_observer=a).run(sequence,profile=profile)
  b=Projection(a.rows);runner=m.Runner(candidate,observer,cycle_observer=b);y=runner.run(sequence,profile=profile)
  assert x==y and len(a.rows)==len(b.rows)
  assert runner.lib.sb_idle_probe_pending()==4,'FIFO was not actually saturated'
  assert x['totals']['requests']==2 and x['totals']['completed']==2 and not x['totals']['fault']
  rows.append(dict(phase=phase,profile=profile,pass_=True,projection_sha256=hashlib.sha256(b''.join(a.rows)).hexdigest(),cycles=len(a.rows),totals=x['totals']))
out.write_text(json.dumps(dict(result='PASS',cases=rows,scope='actual linked kernels and full original PIO/GPIO projection; no observer consumer, full local FIFO; not physical power/arbitration proof'),indent=2)+'\n');print('PASS16 local-full-FIFO/noninterference cases')
