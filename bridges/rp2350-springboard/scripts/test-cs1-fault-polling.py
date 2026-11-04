#!/usr/bin/env python3
"""Count real Core0 fault publications in linked ARM code; no hardware access.

No SRAM arbitration is modeled. The test proves repeated work is removed, not
that the removal fixes physical ROM installation. Use --expect-repeated only
for the frozen A2 control, never to qualify a new candidate.
"""
import argparse,hashlib,json,struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
from unicorn.arm_const import *

def run(elf,repeated):
 with elf.open('rb') as f:
  e=ELFFile(f);s={x.name:x['st_value'] for x in e.get_section_by_name('.symtab').iter_symbols()};segments=[(x['p_vaddr'],x.data()) for x in e.iter_segments() if x['p_type']=='PT_LOAD']
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
 for a,n in ((0x10000000,0x1000000),(0x20000000,0x100000),(0x40000000,0x2000000),(0x50000000,0x500000),(0xd0000000,0x1000)):u.mem_map(a,n)
 for a,b in segments:
  if b:u.mem_write(a,b)
 ret,stack=0x2007f100,0x2007e000
 def call(name,*args):
  u.reg_write(UC_ARM_REG_SP,stack);u.reg_write(UC_ARM_REG_LR,ret|1)
  for reg,v in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args):u.reg_write(reg,v)
  u.emu_start(s[name]|1,ret,count=1000000);assert u.reg_read(UC_ARM_REG_PC)==ret
 def put(a,v):u.mem_write(a,struct.pack('<I',v))
 def word(off):return int.from_bytes(u.mem_read(s['table']+off,2),'little')
 reads=[];stores=[]
 def rd(u,access,a,n,v,_):
  if a in (0x50200008,0x50400008,0x50400030):reads.append(a)
 def wr(u,access,a,n,v,_):
  if s['table']<=a<s['table']+65536:stores.append(dict(address=a,size=n,value=v))
 u.hook_add(UC_HOOK_MEM_READ,rd);u.hook_add(UC_HOOK_MEM_WRITE,wr)
 call('sb_stage_a_init',s['worker'],s['table']);reads.clear();stores.clear();rom=bytes(u.mem_read(s['words'],65536))
 put(0x50400030,32)
 call('writes');first=list(stores);assert len(first)==2 and [x['value'] for x in first]==[7,24]
 for i in range(1023):call('writes')
 storm=dict(polls=1024,table_stores=len(stores),capture_mmio_reads=len(reads))
 assert storm['table_stores']==(2048 if repeated else 2),storm
 assert storm['capture_mmio_reads']==(3072 if repeated else 3),storm
 assert word(6)==24 and word(24)==7 and word(28)==0
 assert int.from_bytes(u.mem_read(s['worker']+4,4),'little')==0
 # Model the table/capture reset after explicit quiescent recovery. Full stop
 # handshake and W1C ordering remain covered by test-cs1-msc-control.py.
 put(0x50400030,0);call('sb_stage_a_init',s['worker'],s['table']);put(0x50200004,0x400)
 reads.clear();stores.clear()
 for i in range(64):call('writes')
 assert not stores and word(6)==1 and word(24)==0
 put(0x50200008,4);call('writes');assert len(stores)==2 and word(6)==24 and word(24)==7
 assert bytes(u.mem_read(s['words'],65536))==rom
 return dict(result='PASS',elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),expected_repeated_control=repeated,storm=storm,fresh_fault_after_reinit=True,ROM_unchanged=True,scope='Actual ARM writes()/fault publication with mapped MMIO; no dual-core memory contention or physical timing claim')

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('elf',type=Path);p.add_argument('--expect-repeated',action='store_true');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists():p.error('output must be new')
 r=run(a.elf,a.expect_repeated);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r['storm']))
if __name__=='__main__':main()
