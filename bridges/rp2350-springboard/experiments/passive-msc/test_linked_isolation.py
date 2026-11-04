#!/usr/bin/env python3
"""Cycle-for-cycle idle-CS1 differential gate over the frozen ROM-drive ELF.

This tests a passive edge observer, not a complete CS1 responder. No firmware,
USB, Palm, clock, pin, or output-owner changes are made on hardware.
"""
import argparse,ctypes as C,gzip,hashlib,importlib.util,json
from pathlib import Path
spec=importlib.util.spec_from_file_location('rom_test',Path(__file__).resolve().parents[2]/'scripts/test-persistent-reply.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Recorder:
 def __init__(self,expected=None):self.expected=expected;self.rows=[];self.n=0
 def __call__(self,lib):
  buf=(C.c_uint32*1024)();lib.sb_idle_snapshot.argtypes=[C.POINTER(C.c_uint32),C.c_uint]
  count=lib.sb_idle_snapshot(buf,1024);assert count<=1024
  raw=bytes(buf)[:count*4]
  assert lib.sb_idle_probe_quiet(),('CS1 observer not passive',lib.sb_cosim_clock())
  if self.expected is not None:
   assert self.n<len(self.expected) and raw==self.expected[self.n],('CS0 cycle divergence',self.n)
  self.rows.append(raw);self.n+=1

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('elf',type=Path)
 p.add_argument('reference_library',type=Path);p.add_argument('passive_library',type=Path)
 p.add_argument('--candidate-elf',type=Path,required=True);p.add_argument('--baseline-proof',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 proof=json.loads(a.baseline_proof.read_text())
 assert hashlib.sha256(a.elf.read_bytes()).hexdigest()==proof['elf_sha256'],'reference ELF differs from frozen hardware baseline'
 from elftools.elf.elffile import ELFFile
 def kernel(path):
  with path.open('rb') as f:
   e=ELFFile(f);sym=next(x for x in e.get_section_by_name('.symtab').iter_symbols() if x.name=='sb_persistent_reply');sec=e.get_section(sym['st_shndx']);offset=(sym['st_value']&~1)-sec['sh_addr'];return sym['st_value'],sec.data()[offset:offset+sym['st_size']+12]
 assert kernel(a.elf)==kernel(a.candidate_elf),'kernel bytes/address or adjacent literal pool differ'
 a.output.mkdir(parents=True,exist_ok=False)
 report=dict(candidate_elf_sha256=hashlib.sha256(a.candidate_elf.read_bytes()).hexdigest(),frozen_baseline_proof=str(a.baseline_proof),frozen_ARM=proof['critical_functions']['sb_persistent_reply'],backend='mapped-register',result='RUNNING',scope='Steady armed CS0 with CS1 inactive; no Core0 arbiter/USB contention or physical timing model',elf_sha256=hashlib.sha256(a.elf.read_bytes()).hexdigest(),cases=[],failures=[])
 def save(): (a.output/'results.json').write_text(json.dumps(report,indent=2)+'\n')
 save();cases={}
 for phase in range(8):
  t=80+phase
  for gap in (5,6,7,8):
   cases[f'pair-{phase}-{gap}']=([(t,t+68,8,m.READ),(t+68+gap,t+136+gap,10,m.READ)],{})
  for width in (0,4,12,32,60):
   for label,endctrl in [('cs',m.IDLE),('oe',m.READ|(1<<18)),('reset',m.READ&~m.RESET)]:
    cases[f'cancel-{phase}-{width}-{label}']=([(t,t+width,8,m.READ),(t+width,t+width+8,8,endctrl),(500,600,0x444,m.READ)],{})
 for at in (80,100,130,145,160,180,200):
  cases[f'cpu-stall-{at}']=([(80,148,8,m.READ),(154,222,10,m.READ),(700,900,0x444,m.READ)],dict(stall=(at,500)))
 cases['idle-forever']=([(100,1200,0,m.IDLE)],{})
 cases['reset-no-cs1']=([(100,500,0,15<<16),(600,700,8,m.READ)],{})
 cases['unselected-we']=([(100,164,0,m.RESET|(7<<16)),(300,400,8,m.READ)],{})
 cases['unselected-oe']=([(100,164,0,m.RESET|(11<<16)),(300,400,8,m.READ)],{})
 cases['repeated-address']=([(100+i*76,168+i*76,8,m.READ) for i in range(16)],{})
 cases['changing-address']=([(100+i*76,168+i*76,(i*2),m.READ) for i in range(32)],{})
 cases['contrasting-data']=([(100,168,0x100,m.READ),(174,242,0x102,m.READ)],{})
 cases['high-unmapped']=([(100,168,0x10000,m.READ),(176,244,0x10002,m.READ)],{})
 for profile in (1,2):
  for name,(seq,opts) in cases.items():
   entry=dict(name=name,profile=profile)
   try:
    reference=Recorder();base=m.Runner(a.elf,a.reference_library,'shadow',reference).run(seq,profile=profile,**opts)
    observer=Recorder(reference.rows);extra=m.Runner(a.candidate_elf,a.passive_library,'shadow',observer).run(seq,profile=profile,**opts)
    assert len(observer.rows)==len(reference.rows),'different PIO clock count'
    assert base==extra,'ARM sequence/events/completions differ'
    entry.update(pass_=True,cycles=len(reference.rows),projection_words=len(reference.rows[0])//4,projection_sha256=hashlib.sha256(b''.join(reference.rows)).hexdigest(),totals=base['totals'],events=base['events'])
    path=a.output/'traces'/f'{name}-p{profile}';path.parent.mkdir(exist_ok=True)
    with gzip.open(str(path)+'.projection.bin.gz','wb') as f:f.write(b''.join(reference.rows))
    with gzip.open(str(path)+'.arm.json.gz','wt') as f:json.dump(base['trace'],f)
   except AssertionError as e:
    entry.update(pass_=False,error=repr(e));report['failures'].append(entry.copy())
   report['cases'].append(entry)
   save()
 # Positive activation: a CS1 edge produces one untrusted snapshot, never a reply.
 lib=C.CDLL(str(a.passive_library.resolve()));lib.sb_cosim_bus.argtypes=[C.c_uint64,C.c_uint]
 assert lib.sb_cosim_init()==0
 lib.sb_cosim_bus(m.IDLE,0);assert lib.sb_cosim_step(16)==0 and lib.sb_idle_probe_quiet()
 lib.sb_cosim_bus(m.RESET|(9<<16),0);assert lib.sb_cosim_step(16)==0
 assert lib.sb_idle_probe_pending()==1 and not lib.sb_cosim_driven()
 value=lib.sb_idle_probe_pop();assert value&0x2000000f==0x20000009
 lib.sb_cosim_bus(m.IDLE,0);assert lib.sb_cosim_step(16)==0 and lib.sb_idle_probe_quiet()
 report['actual_CS1_edge_observed_without_driving']=True
 # CS may precede OE; do not publish until a read/write-shaped sample appears.
 lib.sb_cosim_bus(m.RESET|(13<<16),0);assert lib.sb_cosim_step(24)==0
 assert lib.sb_idle_probe_pending()==0
 lib.sb_cosim_bus(m.RESET|(9<<16),0);assert lib.sb_cosim_step(24)==0
 assert lib.sb_idle_probe_pending()==1 and not lib.sb_cosim_driven()
 lib.sb_idle_probe_pop();lib.sb_cosim_bus(m.IDLE,0)
 assert lib.sb_cosim_step(16)==0 and lib.sb_idle_probe_quiet()
 report['CS_before_OE_observed']=True
 for ctrl in ((9<<16),m.RESET|(8<<16),m.RESET|(1<<16)):
  lib.sb_cosim_bus(ctrl,0);assert lib.sb_cosim_step(24)==0
  assert lib.sb_idle_probe_pending()==0 and not lib.sb_cosim_driven()
  lib.sb_cosim_bus(m.IDLE,0);assert lib.sb_cosim_step(16)==0 and lib.sb_idle_probe_quiet()
 report['reset_and_malformed_CS1_edges_do_not_publish']=True
 # Negative control: a miswired observer must be rejected by the passivity gate.
 rejected=False;assert lib.sb_cosim_init()==0;lib.sb_idle_probe_mutate()
 lib.sb_cosim_bus(m.READ,8)
 for i in range(32):
  error=lib.sb_cosim_step(1)
  if error or not lib.sb_idle_probe_quiet():rejected=True;break
 assert rejected,'negative control incorrectly accepted'
 report['miswired_observer_rejected']=True
 report['result']='FAIL' if report['failures'] else 'PASS';save()
 print(report['result'],len(report['cases'])-len(report['failures']),'/',len(report['cases']),report['failures'][:3])
 raise SystemExit(bool(report['failures']))
if __name__=='__main__':main()
