#!/usr/bin/env python3
"""Actual linked Core0 CS1 retirement/status/recovery; no device or UART access.

Core1 stop acknowledgment and timer/MMIO are modeled. The separate full ARM/PIO
suite tests real Core1 instructions, capture, release and cancellation.
"""
import argparse,json,hashlib,struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.arm_const import *
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('elf',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 with a.elf.open('rb') as f:
  e=ELFFile(f);s={x.name:x['st_value'] for x in e.get_section_by_name('.symtab').iter_symbols()}
  seg=[(x['p_vaddr'],x.data()) for x in e.iter_segments() if x['p_type']=='PT_LOAD']
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
 for at,n in ((0x10000000,0x1000000),(0x20000000,0x100000),(0x40000000,0x2000000),(0x50000000,0x500000),(0xd0000000,0x1000),(0xe000e000,0x2000)):u.mem_map(at,n)
 for at,b in seg:
  if b:u.mem_write(at,b)
 RET,STACK=0x2007f100,0x2007e000
 pending=[]
 def w1c(u,access,at,n,value,_):
  if at in (0x50200008,0x50400008):
   pending.append((at,int.from_bytes(u.mem_read(at,4),'little')&~value))
 def hook(u,at,n,_):
  while pending:
   address,value=pending.pop(0);u.mem_write(address,struct.pack('<I',value))
  if at==(s['hard_assertion_failure']&~1):raise AssertionError('unexpected SDK hard assertion')
 u.hook_add(UC_HOOK_CODE,hook);u.hook_add(UC_HOOK_MEM_WRITE,w1c)
 def call(name,*args,tail=()):
  u.reg_write(UC_ARM_REG_SP,STACK);u.reg_write(UC_ARM_REG_LR,RET|1)
  for r,v in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args):u.reg_write(r,v)
  for i,v in enumerate(tail):u.mem_write(STACK+4*i,struct.pack('<I',v))
  u.emu_start(s[name]|1,RET,count=2000000)
  assert u.reg_read(UC_ARM_REG_PC)==RET,(name,hex(u.reg_read(UC_ARM_REG_PC)))
  return u.reg_read(UC_ARM_REG_R0)
 def flag(name,n):u.mem_write(s[name],bytes([n]))
 def byte(name):return u.mem_read(s[name],1)[0]
 def word(off):return int.from_bytes(u.mem_read(s['table']+off,2),'little')
 def retire(token,epoch,outcome=0,fault=0):return call('sb_stage_a_retire',s['worker'],0x5001a,0x20000005|(token<<4),epoch&0xffffffff,tail=(outcome,fault))
 cases=[];romhash=hashlib.sha256(u.mem_read(s['words'],65536)).hexdigest()
 call('sb_stage_a_init',s['worker'],s['table']);assert word(0)==0x464e and word(2)==0x100 and word(6)==1 and word(28)==0 and word(128)==0xffff
 assert retire(1,0)==1 and word(28)==1
 assert retire(1,-1)==1 and word(28)==1
 assert retire(2,-2,0xffffffff)==1 and word(28)==1
 assert retire(2,-3)==1 and word(28)==2
 call('status');n=int.from_bytes(u.mem_read(s['out_used'],4),'little');r=json.loads(u.mem_read(s['out'],n))
 assert r['cs1']['ack']==2 and r['cs1']['accepted']==2 and r['cs1']['status']==1
 cases.extend(('fixed-values','accepted-token1','duplicate-no-effect','cancel-no-effect','next-token2','status-accounting'))
 assert retire(3,1)==0 and word(6)==24 and word(24)==7
 assert retire(3,-4)==0 and word(28)==2
 cases.extend(('bad-epoch-fault','sticky-reject'))
 flag('rom_ok',1);flag('ready',1);flag('active',1);flag('stopped',0)
 u.mem_write(0xd0000004,struct.pack('<II',0,0))
 call('recover_request');assert not byte('stop') and not byte('recovering');cases.append('nonidle-recovery-refused')
 u.mem_write(0xd0000004,struct.pack('<II',0xf0000,0x2000))
 u.mem_write(0x50200008,struct.pack('<I',0x10004));u.mem_write(0x50400008,struct.pack('<I',0x20006))
 call('recover_request');assert byte('stop') and byte('recovering')
 call('recover_step');assert byte('stop') and byte('recovering') and word(28)==2
 assert int.from_bytes(u.mem_read(0x50200008,4),'little')==0x10004
 cases.append('no-reinitialization-before-core1-stop')
 # Modeled acknowledgment only after the real Core1 would clamp/park.
 flag('active',0);flag('stopped',1)
 call('recover_step');assert not byte('stop') and not byte('recovering') and byte('arm')
 assert word(28)==0 and word(6)==1 and word(24)==0
 assert int.from_bytes(u.mem_read(0x50200008,4),'little')==0x10000 and int.from_bytes(u.mem_read(0x50400008,4),'little')==0x20000
 cases.append('capture-W1C-cleared-only-after-stop')
 cases.append('explicit-quiescent-recovery')
 assert hashlib.sha256(u.mem_read(s['words'],65536)).hexdigest()==romhash
 cases.append('ROM-table-unchanged')
 a.output.write_text(json.dumps(dict(result='PASS',elf_sha256=hashlib.sha256(a.elf.read_bytes()).hexdigest(),cases=cases,scope='Actual linked M33 Core0 functions; stopped handshake modeled, not physical bus timing'),indent=2)+'\n');print('PASS',len(cases),'linked Core0 retirement/status/recovery cases')
if __name__=='__main__':main()
