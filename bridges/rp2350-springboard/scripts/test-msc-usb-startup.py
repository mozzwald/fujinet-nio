#!/usr/bin/env python3
"""Execute linked SDK USB IRQ registration, including its hard-assert contract.

Mapped MMIO; real vector repair, spinlocks, IRQ registration and TinyUSB init run.
Physical USB reset completion is modeled. No device access or wire-level proof.
"""
import argparse,hashlib,json,struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.arm_const import *

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('elf',type=Path);p.add_argument('--expect-assert',action='store_true');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 with a.elf.open('rb') as f:
  e=ELFFile(f);s={x.name:x['st_value'] for x in e.get_section_by_name('.symtab').iter_symbols()}
  seg=[(x['p_vaddr'],x.data()) for x in e.iter_segments() if x['p_type']=='PT_LOAD']
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
 for addr,size in ((0x10000000,0x1000000),(0x20000000,0x100000),(0x40000000,0x2000000),(0x50000000,0x400000),(0xd0000000,0x1000),(0xe000e000,0x2000)):
  u.mem_map(addr,size)
 for addr,b in seg:
  if b:u.mem_write(addr,b)
 # Hardware spinlocks return a nonzero acquisition flag; software-lock RAM is 0.
 for addr in range(0xd0000100,0xd0000180,4):u.mem_write(addr,struct.pack('<I',1))
 # Model peripheral reset release; no USB packets/controller IRQs synthesized.
 u.mem_write(0x40020008,struct.pack('<I',0xffffffff))
 ret,stack=0x2007f100,0x2007e000
 outcome={'asserted':False,'calls':[],'instructions':0}
 def code(u,at,size,_):
  outcome['instructions']+=1
  if at==(s['hard_assertion_failure']&~1):
   outcome['asserted']=True;outcome['assert_lr']=hex(u.reg_read(UC_ARM_REG_LR));u.emu_stop()
  if at==(s.get('irq_add_shared_handler',0)&~1):
   outcome['calls'].append({'function':'irq_add_shared_handler','irq':u.reg_read(UC_ARM_REG_R0),'handler':hex(u.reg_read(UC_ARM_REG_R1)),'primask':u.reg_read(UC_ARM_REG_PRIMASK)})
 def writes(u,access,at,size,value,_):
  # RP atomic aliases for reset/USB/IO register blocks over RAM-backed MMIO.
  for base in (0x40020000,0x40028000,0x50110000):
   if base+0x1000<=at<base+0x4000:
    mode=(at-base)//0x1000;target=at-mode*0x1000
    prior=int.from_bytes(u.mem_read(target,4),'little')
    n=prior^value if mode==1 else prior|value if mode==2 else prior&~value
    u.mem_write(target,struct.pack('<I',n&0xffffffff))
 u.hook_add(UC_HOOK_CODE,code);u.hook_add(UC_HOOK_MEM_WRITE,writes)
 def call(name,*args):
  u.reg_write(UC_ARM_REG_SP,stack);u.reg_write(UC_ARM_REG_LR,ret|1)
  for r,value in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args):u.reg_write(r,value)
  u.emu_start(s[name]|1,ret,count=2000000)
  if not outcome['asserted']:assert u.reg_read(UC_ARM_REG_PC)==ret,(name,hex(u.reg_read(UC_ARM_REG_PC)))
  return u.reg_read(UC_ARM_REG_R0)
 u.mem_write(s['ram_vector_table'],bytes(u.mem_read(s['__vectors'],272)))
 u.mem_write(0xe000ed08,struct.pack('<I',s['ram_vector_table']))
 call('install_ram_exceptions')
 slot=s['ram_vector_table']+(16+14)*4
 before=int.from_bytes(u.mem_read(slot,4),'little');assert before==s['sb_msc_fault_park']
 if a.expect_assert:
  call('irq_add_shared_handler',14,s['dcd_rp2040_irq'],255)
  assert outcome['asserted'],'old firmware did not reproduce SDK assert'
 else:
  assert call('initialize_usb')==1
  assert not outcome['asserted']
  after=int.from_bytes(u.mem_read(slot,4),'little')
  assert s['irq_handler_chain_slots']<=after<s['irq_handler_chain_slots']+48
  assert after!=before and after&1
  assert all(0x20000000<=v<0x20082000 for v in struct.unpack('<68I',u.mem_read(s['ram_vector_table'],272))[2:])
  assert outcome['calls'] and all(c['primask']==1 for c in outcome['calls'])
  assert u.reg_read(UC_ARM_REG_PRIMASK)==0
  outcome['usb_pullup_enabled']=bool(int.from_bytes(u.mem_read(0x5011004c,4),'little')&(1<<16))
  assert outcome['usb_pullup_enabled']
 outcome.update(elf_sha256=hashlib.sha256(a.elf.read_bytes()).hexdigest(),result='EXPECTED_OLD_ASSERT_REPRODUCED' if a.expect_assert else 'PASS',scope='Actual SDK IRQ registration and TinyUSB initialization with mapped MMIO; not physical USB enumeration')
 a.output.write_text(json.dumps(outcome,indent=2)+'\n');print(outcome['result'])
if __name__=='__main__':main()
