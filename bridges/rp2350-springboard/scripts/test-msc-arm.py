#!/usr/bin/env python3
"""Run actual linked MSC callbacks/vector repair with mapped-register Unicorn.

NOR erase/program and endpoint0 transmission are replaced; real COW/CRC and
MSC class/control callbacks execute.
Not a dual-core arbitration, USB host, or analog timing simulation.
"""
import argparse,hashlib,json,struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.arm_const import *

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('elf',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 with a.elf.open('rb') as f:
  e=ELFFile(f);syms={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
  segments=[(s['p_vaddr'],s.data()) for s in e.iter_segments() if s['p_type']=='PT_LOAD']
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
 for at,n in [(0x10000000,0x1000000),(0x20000000,0x100000),(0x40000000,0x2000000),(0x50000000,0x400000),(0xd0000000,0x1000),(0xe000e000,0x2000)]:u.mem_map(at,n)
 u.mem_write(0x10000000,b'\xff'*0x1000000)
 for at,b in segments:
  if b:u.mem_write(at,b)
 RET,STACK,TMP=0x2007f100,0x2007e000,0x20070000
 operations=[];control_reply=[];fault_test=[False]
 def hook(u,at,size,_):
  if at==(syms['flash_range_erase']&~1):
   off=u.reg_read(UC_ARM_REG_R0);n=u.reg_read(UC_ARM_REG_R1)
   assert 0x100000<=off<=0xd00000-n;u.mem_write(0x10000000+off,b'\xff'*n);operations.append('erase');u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
  elif at==(syms['flash_range_program']&~1):
   off=u.reg_read(UC_ARM_REG_R0);ptr=u.reg_read(UC_ARM_REG_R1);n=u.reg_read(UC_ARM_REG_R2)
   assert 0x100000<=off<=0xd00000-n and 0x20000000<=ptr<0x20082000
   data=bytes(u.mem_read(ptr,n));prior=bytes(u.mem_read(0x10000000+off,n));assert all((x&y)==y for x,y in zip(prior,data))
   u.mem_write(0x10000000+off,data);operations.append('program');u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
  elif at==(syms['tud_control_xfer']&~1):
   ptr=u.reg_read(UC_ARM_REG_R2);n=u.reg_read(UC_ARM_REG_R3)
   control_reply.append(bytes(u.mem_read(ptr,n)));u.reg_write(UC_ARM_REG_R0,1)
   u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
  elif fault_test[0] and bytes(u.mem_read(at,2))==b'\x20\xbf':u.emu_stop() # WFE park
 def alias(u,access,at,size,value,_):
  # RP atomic register aliases are modeled explicitly over plain MMIO RAM.
  if 0x40029000<=at<0x4002c000:
   mode=(at-0x40028000)//0x1000;base=at-mode*0x1000
   prior=int.from_bytes(u.mem_read(base,4),'little')
   new=prior^value if mode==1 else prior|value if mode==2 else prior&~value
   u.mem_write(base,struct.pack('<I',new&0xffffffff))
 u.hook_add(UC_HOOK_MEM_WRITE,alias)
 u.hook_add(UC_HOOK_CODE,hook)
 def call(name,*args,tail=()):
  u.reg_write(UC_ARM_REG_SP,STACK);u.reg_write(UC_ARM_REG_LR,RET|1)
  for r,n in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args):u.reg_write(r,n)
  for i,n in enumerate(tail):u.mem_write(STACK+4*i,struct.pack('<I',n))
  u.emu_start(syms[name]|1,RET,count=4000000)
  assert u.reg_read(UC_ARM_REG_PC)==RET,(name,hex(u.reg_read(UC_ARM_REG_PC)))
  return u.reg_read(UC_ARM_REG_R0)
 def flag(name,n):u.mem_write(syms[name],bytes([n]))
 # Repair the real default vector image in SRAM, then confirm every exception
 # and IRQ entry uses RAM. Reset is deliberately excluded.
 vector=bytes(u.mem_read(syms['__vectors'],272));u.mem_write(syms['ram_vector_table'],vector)
 u.mem_write(0xe000ed08,struct.pack('<I',syms['ram_vector_table']))
 call('install_ram_exceptions')
 vectors=struct.unpack('<68I',u.mem_read(syms['ram_vector_table'],272))
 assert all(0x20000000<=v<0x20082000 and v&1 for v in vectors[2:])
 assert u.mem_read(syms['ram_island_ok'],1)==b'\1'
 flag('flash_size_ok',1);flag('boot_phase',0);flag('active',1)
 # AAPCS struct rs_io passed by value after disk pointer (20 bytes total).
 assert call('ms_init',syms['disk'],0,syms['rd']|1,syms['er']|1,tail=(syms['pg']|1,syms['safe']|1))==1
 active_table=hashlib.sha256(u.mem_read(syms['words'],65536)).hexdigest()
 assert call('tud_msc_get_maxlun_cb')==1
 u.mem_write(TMP,struct.pack('<BBHHH',0xa1,0xfe,0,2,1))
 assert call('mscd_control_xfer_cb',0,1,TMP)==1 and control_reply==[b'\0']
 assert call('tud_msc_test_unit_ready_cb',0)==1
 call('tud_msc_capacity_cb',0,TMP,TMP+4);assert struct.unpack('<IH',u.mem_read(TMP,6))==(2048,512)
 data=bytes((i*37)%256 for i in range(512));u.mem_write(TMP,data)
 assert call('tud_msc_write10_cb',0,20,0,TMP,tail=(512,))==512
 u.mem_write(TMP,b'\0'*512)
 assert call('tud_msc_read10_cb',0,20,0,TMP,tail=(512,))==512 and u.mem_read(TMP,512)==data
 assert operations==['erase','program','program','program']
 assert call('ms_init',syms['disk'],0,syms['rd']|1,syms['er']|1,tail=(syms['pg']|1,syms['safe']|1))==1
 assert call('tud_msc_read10_cb',0,20,0,TMP,tail=(512,))==512 and u.mem_read(TMP,512)==data
 assert call('tud_msc_read10_cb',0,2048,0,TMP,tail=(1,))==0xffffffff
 assert call('tud_msc_write10_cb',0,0,512,TMP,tail=(1,))==0xffffffff
 assert call('er',0,0xfc0000)==0 and call('pg',0,0xfc0000,TMP)==0
 u.mem_write(TMP,b'\x35'+bytes(15));assert call('tud_msc_scsi_cb',0,TMP,TMP+32,0)==0
 assert call('tud_msc_start_stop_cb',0,0,0,1)==1
 assert call('tud_msc_test_unit_ready_cb',0)==0
 assert call('tud_msc_start_stop_cb',0,0,1,1)==1
 assert call('tud_msc_test_unit_ready_cb',0)==1
 call('tud_umount_cb')
 assert u.mem_read(syms['active'],1)==b'\1' and hashlib.sha256(u.mem_read(syms['words'],65536)).hexdigest()==active_table
 # Exception handler sets independent pad overrides and parks without XIP.
 fault_test[0]=True
 u.reg_write(UC_ARM_REG_SP,STACK);u.emu_start(syms['sb_msc_fault_park']|1,RET,count=10000)
 assert u.mem_read(syms['exception_fault'],1)==b'\1'
 # RP2350 GPIO CTRL: OEOVER bits 14..15, value 2 = forced disabled.
 for pin in range(20,36):assert struct.unpack('<I',u.mem_read(0x40028004+pin*8,4))[0]&(3<<14)==2<<14
 a.output.write_text(json.dumps(dict(result='PASS',elf_sha256=hashlib.sha256(a.elf.read_bytes()).hexdigest(),
  cases=['GET_MAX_LUN-stack-request','runtime-vector-repair','capacity','write-read','persistent-rescan','LBA-offset-bounds','private-write-rejection-live','flush','eject-load','detach-no-activation','fault-pad-clamp-park'],
  flash_operations=operations,live_rom_table_unchanged=True,
  scope='Actual linked ARM callbacks with mapped MMIO; NOR and endpoint0 transmission mocked; no USB wire or dual-core physical timing proof'),indent=2)+'\n')
 print('PASS actual ARM MSC callbacks, runtime vectors, persistent sectors, fault clamp and immutable live ROM')
if __name__=='__main__':main()
