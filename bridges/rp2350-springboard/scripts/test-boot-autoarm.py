#!/usr/bin/env python3
"""Execute linked Core1 startup scheduling; peripheral init/kernel are stubs.

This validates boot policy and guards, not analog ownership or M33 cycle timing.
The separately retained actual-ARM/PIO suite validates unchanged response code.
"""
import argparse,json,struct,hashlib
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE
from unicorn.arm_const import *

def run(elf,rom,*,valid=True,low=0x50000,high=0x2000,release=False,stop_wait=False,fault=False):
 with elf.open('rb') as f:
  e=ELFFile(f);syms={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
  seg=[(s['p_vaddr'],s.data()) for s in e.iter_segments() if s['p_type']=='PT_LOAD' and 0x20000000<=s['p_vaddr']<0x20100000]
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
 for addr,size in [(0x20000000,0x100000),(0xd0000000,0x1000),(0x40000000,0x2000000),(0x50000000,0x400000)]:u.mem_map(addr,size)
 for addr,data in seg:u.mem_write(addr,data)
 u.mem_write(syms['bytes'],rom+bytes([255])*(65536-len(rom)))
 if 'image_length' in syms:u.mem_write(syms['image_length'],struct.pack('<I',len(rom)))
 u.mem_write(syms['rom_ready'],bytes([valid]));u.mem_write(0xd0000004,struct.pack('<II',low,high))
 u.reg_write(UC_ARM_REG_SP,0x200ff000);u.reg_write(UC_ARM_REG_LR,0x200fe001)
 state=dict(pio_init=0,lookup=0,kernel=0,instructions=0,wait_instructions=0,released=False)
 def byte(name):return u.mem_read(syms[name],1)[0]
 def hook(u,addr,size,_):
  state['instructions']+=1
  if addr==(syms['sb_rom_word']&~1):
   assert u.reg_read(UC_ARM_REG_R0)==syms['bytes'] and u.reg_read(UC_ARM_REG_R1)==len(rom), 'ROM source/length not owned by this image'
   offset=u.reg_read(UC_ARM_REG_R2);value=int.from_bytes(rom[offset:offset+2],'big') if offset+2<=len(rom) else 0xffff
   u.reg_write(UC_ARM_REG_R0,value);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR));state['lookup']+=1;return
  if addr==(syms['sb_pio_rearm_program_init']&~1):
   state['pio_init']+=1;u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR));return
  if state['pio_init'] and not byte('arm') and not byte('active') and not byte('stopped'):
   state['wait_instructions']+=1
   if state['wait_instructions']==250:
    if release:
     u.mem_write(0xd0000004,struct.pack('<II',0x50000,0x2000));state['released']=True
    if stop_wait:u.mem_write(syms['stop'],b'\1')
  if addr==(syms['sb_persistent_reply']&~1):
   state['kernel']+=1;assert byte('active')==1
   u.mem_write(u.reg_read(UC_ARM_REG_R0),struct.pack('<6I',0,0,0,0,0,int(fault)))
   u.mem_write(syms['stop'],b'\1');u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR));return
  # Execute another idle interval after stop/fault to catch unwanted rearming.
  if state['kernel'] and byte('stopped'):
   state['idle_after_stop']=state.get('idle_after_stop',0)+1
   if state['idle_after_stop']>=500:u.emu_stop()
  if state['wait_instructions']>=1000 and not release and not stop_wait:u.emu_stop()
  if stop_wait and byte('stopped') and state['pio_init']:
   state['idle_after_stop']=state.get('idle_after_stop',0)+1
   if state['idle_after_stop']>=500:u.emu_stop()
  if not valid and state['instructions']>=2000:u.emu_stop()
 u.hook_add(UC_HOOK_CODE,hook);u.emu_start(syms['sb_boot_autoarm_entry']|1,0,count=2000000)
 state.update({k:byte(k) for k in ('ready','arm','active','stopped','owner_fault')})
 if valid:
  assert state['pio_init']==1 and state['lookup']==32768,state
  assert bytes(u.mem_read(syms['words'],65536))==b''.join(rom[i:i+2][::-1] for i in range(0,len(rom),2))+bytes([255])*(65536-len(rom))
 else:assert state['pio_init']==state['lookup']==state['kernel']==0 and state['stopped'] and not state['arm'],state
 should_run=valid and (low==0x50000 and high==0x2000 or release) and not stop_wait
 assert state['kernel']==int(should_run),state
 if should_run or stop_wait:
  assert state['stopped'] and not state['active'] and not state['arm'],state
 if fault:assert state['owner_fault'] and not state['ready'],state
 return state

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('elf',type=Path);p.add_argument('rom',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 cases={'valid-no-USB':{},'invalid-ROM':dict(valid=False),'CS0-selected':dict(low=0x40000),'OE-selected':dict(low=0x10000),'reset-held':dict(high=0),'release-reset':dict(high=0,release=True),'stop-while-waiting':dict(high=0,stop_wait=True),'owner-fault-no-retry':dict(fault=True)}
 results={k:run(a.elf,a.rom.read_bytes(),**v) for k,v in cases.items()}
 a.output.write_text(json.dumps(dict(result='PASS',elf_sha256=hashlib.sha256(a.elf.read_bytes()).hexdigest(),rom_sha256=hashlib.sha256(a.rom.read_bytes()).hexdigest(),cases=results,scope='linked Core1 startup, native mapped registers; PIO init and response kernel stubbed; not bus/electrical qualification'),indent=2)+'\n');print('PASS: eight compiled boot scheduling/guard/stop/fault cases')
if __name__=='__main__':main()
