#!/usr/bin/env python3
"""CS1 input activity must not change ROM traces or output ownership.

One unconsumed input descriptor is deliberately left pending: no Core0 service
or CS1 reply is implied. Only private PIO0 IRQ7 is omitted from ROM projection.
"""
from pathlib import Path
import argparse,ctypes as C,gzip,importlib.util,json
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('frozen',root/'scripts/test-persistent-reply.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Recorder:
 def __init__(self,expected=None):self.expected=expected;self.rows=[]
 def __call__(self,lib):
  buf=(C.c_uint32*1024)();lib.sb_idle_snapshot.argtypes=[C.POINTER(C.c_uint32),C.c_uint]
  n=lib.sb_idle_snapshot(buf,1024)
  assert n<=1024
  for k in (5,6,7):buf[k]&=~128 # private high-capture rendezvous, not ROM IRQ
  raw=bytes(buf)[:n*4]
  if self.expected is not None:assert len(self.rows)<len(self.expected) and raw==self.expected[len(self.rows)],('ROM state divergence',lib.sb_cosim_clock())
  self.rows.append(raw)
def main():
 p=argparse.ArgumentParser();p.add_argument('elf',type=Path);p.add_argument('reference',type=Path);p.add_argument('capture',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(exist_ok=False)
 cases={};cs1=m.RESET|(9<<16)
 for gap in (5,6,7,8):
  t=168+gap
  cases[f'rom-reg-rom-{gap}']=[(100,168,8,m.READ),(t,t+68,2,cs1),(t+68+gap,t+168+gap,10,m.READ)]
  cases[f'reg-rom-{gap}']=[(100,168,2,cs1),(t,t+100,8,m.READ)]
  cases[f'reg-gap-reg-rom-{gap}']=[(100,168,2,cs1),(t,t+68,4,cs1),(500,600,8,m.READ)]
 cases['cs1-short']=[(100,106,8,cs1),(112,212,10,m.READ)]
 cases['cs1-before-oe']=[(80,100,8,cs1|(1<<18)),(100,168,8,cs1),(174,274,10,m.READ)]
 cases['cs1-reset']=[(100,130,8,cs1),(130,190,8,cs1&~m.RESET),(500,600,10,m.READ)]
 cases['cs1-write']=[(100,168,26,m.RESET|(5<<16)),(174,274,8,m.READ)]
 report=[]
 for name,seq in cases.items():
  for profile in (1,2):
   old=Recorder();before=m.Runner(a.elf,a.reference,'shadow',old).run(seq,profile=profile)
   new=Recorder(old.rows);after=m.Runner(a.elf,a.capture,'shadow',new).run(seq,profile=profile)
   assert before==after,(name,profile,'ARM trace/events/totals changed')
   assert len(new.rows)==len(old.rows)
   with gzip.open(a.output/f'{name}-p{profile}.json.gz','wt') as f:json.dump(after,f)
   report.append(dict(case=name,profile=profile,identical_ROM=True,cycles=len(old.rows),totals=after['totals']))
 (a.output/'results.json').write_text(json.dumps(dict(passed=len(report),cases=report,scope='Input-only CS1 capture; not a CS1 responder or hardware timing qualification'),indent=2)+'\n')
 print('PASS',len(report),'CS1-active ROM differential cases')
if __name__=='__main__':main()
