#!/usr/bin/env python3
"""Audit actual ARM initializer writes; observer ELF has no firmware startup."""
import argparse,json,struct,hashlib
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_MEM_WRITE
from unicorn.arm_const import *
def execute(path,entry):
 with path.open('rb') as f:
  e=ELFFile(f);syms={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()};segments=[(s['p_vaddr'],s.data()) for s in e.iter_segments() if s['p_type']=='PT_LOAD']
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
 for a,n in ((0x10000000,0x1000000),(0x20000000,0x100000),(0x40000000,0x2000000),(0x50000000,0x500000),(0xd0000000,0x1000)):u.mem_map(a,n)
 for a,b in segments:
  if b:u.mem_write(a,b)
 u.mem_write(0x40020008,struct.pack('<I',0xffffffff));writes=[]
 def wr(uc,access,address,size,value,_):
  if any(a<=address<a+n for a,n in ((0x50200000,0x10000),(0x50300000,0x10000),(0x40028000,0x1000),(0x40038000,0x1000))):writes.append(dict(address=f'{address:08x}',size=size,value=f'{value:08x}'))
 u.hook_add(UC_HOOK_MEM_WRITE,wr);u.reg_write(UC_ARM_REG_SP,0x2007e000);u.reg_write(UC_ARM_REG_LR,0x2007f101);u.reg_write(UC_ARM_REG_R0,0)
 u.emu_start(syms[entry]|1,0x2007f100,count=1000000);assert u.reg_read(UC_ARM_REG_PC)==0x2007f100
 instruction_ram=[[int.from_bytes(u.mem_read(base+0x48+4*i,4),'little') for i in range(32)] for base in (0x50200000,0x50300000)]
 return dict(writes=writes,instruction_ram=instruction_ram,elf_sha256=hashlib.sha256(path.read_bytes()).hexdigest())
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('baseline',type=Path);p.add_argument('observer',type=Path);p.add_argument('--baseline-proof',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 assert not a.output.exists();proof=json.loads(a.baseline_proof.read_text());assert hashlib.sha256(a.baseline.read_bytes()).hexdigest()==proof['elf_sha256']
 b=execute(a.baseline,'sb_pio_rearm_program_init');o=execute(a.observer,'sb_rom_plus_cs1_probe_init')
 assert b['instruction_ram']==o['instruction_ram'],'PIO0/PIO1 instruction RAM changed'
 assert b['writes']==o['writes'],'PIO0/PIO1 or GPIO configuration write sequence changed'
 a.output.write_text(json.dumps(dict(result='PASS',scope='Actual compiled ARM initializer with reset-done/MMIO modeled; not physical startup timing',baseline=b,observer=o),indent=2)+'\n');print('PASS frozen PIO0/PIO1 instruction RAM and complete PIO0/PIO1/GPIO register-write sequence identical')
if __name__=='__main__':main()
