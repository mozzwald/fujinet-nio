#!/usr/bin/env python3
"""Actual unchanged ARM code, ordinary SRAM registers, optional mapped hooks.

The CPU must remain in its completion wait until stop is explicitly set. An
empty WRITE hook plus normal READ hooks may resurrect stale ITSTATE and skip
LDRB(stop), returning while stop==0. Never classify that as a firmware failure.
"""
import argparse,json,struct
from pathlib import Path
import unicorn
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
from unicorn.arm_const import *
from elftools.elf.elffile import ELFFile

def run(path,hooks):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33);u.mem_map(0x20000000,0x100000)
 with path.open('rb') as f:
  e=ELFFile(f);symbols={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
  for s in e.iter_segments():
   if s['p_type']=='PT_LOAD':u.mem_write(s['p_vaddr'],s.data())
 base=0x20080000;regs=base+0x100;gpio=base+0x200;stop=base+0x300;bus=base+0x400;table=0x20040000
 u.mem_write(bus,struct.pack('<III',regs,gpio,stop));u.mem_write(gpio,struct.pack('<I',8));u.mem_write(regs+0x20,struct.pack('<I',0x2000000a));u.mem_write(table+8,b'\xed\xfe')
 for reg,value in [(UC_ARM_REG_R0,base),(UC_ARM_REG_R1,table),(UC_ARM_REG_R2,bus),(UC_ARM_REG_SP,0x200fc000),(UC_ARM_REG_LR,0x200fe001)]:u.reg_write(reg,value)
 pcs=[]
 def code(uc,pc,size,_):
  pcs.append(f'{pc:08x}')
  if uc.reg_read(UC_ARM_REG_LR)==1:u.mem_write(regs+4,struct.pack('<I',0x100))
  if len(pcs)==300:u.mem_write(stop,b'\1')
 def read(uc,access,address,size,value,_):
  if address==regs+4:uc.mem_write(address,struct.pack('<I',0x100 if uc.reg_read(UC_ARM_REG_LR)==1 else 0))
  if address==regs+0x20:uc.mem_write(address,struct.pack('<I',0x2000000a))
 if hooks:
  u.hook_add(UC_HOOK_MEM_READ,lambda *args:None)
  u.hook_add(UC_HOOK_MEM_READ,read,begin=regs,end=regs+0xff)
  u.hook_add(UC_HOOK_MEM_WRITE,lambda *args:None,begin=regs,end=regs+0xff)
 u.hook_add(UC_HOOK_CODE,code);u.emu_start(symbols['sb_persistent_reply'],0x200fe000,count=1000)
 stopped=bytes(u.mem_read(stop,1))==b'\1'
 return dict(hooks=hooks,instructions=len(pcs),stop_set_before_return=stopped,pc=f'{u.reg_read(UC_ARM_REG_PC):08x}',trace=pcs)

def main():
 p=argparse.ArgumentParser();p.add_argument('elf',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();assert not a.output.exists()
 plain=run(a.elf,False);hooked=run(a.elf,True)
 assert plain['stop_set_before_return'] and plain['instructions']>=300,'plain SRAM reference did not execute correctly'
 report=dict(unicorn=unicorn.__version__,plain=plain,mapped_read_write_hooks=hooked,hooked_backend_semantics_valid=hooked['stop_set_before_return'],result='PASS plain reference; hook behavior recorded independently')
 a.output.write_text(json.dumps(report,indent=2)+'\n');print('plain SRAM',plain['instructions'],'stop',plain['stop_set_before_return'],'; mapped read/write hooks',hooked['instructions'],'stop',hooked['stop_set_before_return'])
if __name__=='__main__':main()
