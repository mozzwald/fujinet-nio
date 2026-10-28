#!/usr/bin/env python3
"""Independent container checks and actual linked initializer differential.

Specifically audits this 29-word layout; does not relax the frozen MSC verifier.
No hardware access. Peripheral reset completion is modeled as already released.
"""
import argparse,hashlib,importlib.util,json,struct,subprocess
from pathlib import Path
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_MEM_WRITE
from unicorn.arm_const import *
p=argparse.ArgumentParser();p.add_argument('baseline',type=Path);p.add_argument('candidate',type=Path);p.add_argument('recovery',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
vpath=Path(__file__).resolve().parents[4]/'fujinet-palm/tools/verify-ramsetup-firmware.py'
spec=importlib.util.spec_from_file_location('container',vpath);v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
def image(directory):
 return v.ELF((directory/'springboard_rom_msc.elf').read_bytes())
baseline,candidate=image(a.baseline),image(a.candidate)
def nm(directory):
 rows=subprocess.check_output(['arm-none-eabi-nm','-n',str(directory/'springboard_rom_msc.elf')],text=True).splitlines()
 return {row.split()[-1]:int(row.split()[0],16) for row in rows if len(row.split())==3}
old_symbols,new_symbols=nm(a.baseline),nm(a.candidate)
sha=lambda b:hashlib.sha256(b).hexdigest()
assert candidate.symbol('sb_persistent_reply')==baseline.symbol('sb_persistent_reply'),'ARM kernel changed'
assert candidate.symbols['sb_persistent_reply'][0]==baseline.symbols['sb_persistent_reply'][0],'ARM kernel placement changed'
assert candidate.symbol('sb_rom_image')==baseline.symbol('sb_rom_image')==a.recovery.read_bytes()
for name in ('device','config','sb_rom_image_crc'):
 if name in candidate.symbols:assert candidate.symbol(name)==baseline.symbol(name),name
for name in ('__msc_start','__msc_end','__rom_slot0','__rom_slot1','__rom_metadata0','__rom_metadata1','__rom_storage_end'):
 assert new_symbols[name]==old_symbols[name],name
for e in (baseline,candidate):
 assert (new_symbols if e is candidate else old_symbols)['__flash_binary_end']<=0x10100000
 assert e.symbols['words'][1]==65536 and e.symbols['words'][0]%16==8
 for name in ('bytes','words','import_buffer','disk','storage','service','sb_persistent_reply','sb_pio_rearm_program_init','sb_msc_fault_park'):
  address,n,_,_=e.symbols[name];assert 0x20000000<=address<0x20082000 and address+n<=0x20082000,name
 assert e.symbol('stopped')==b'\1' and e.symbol('arm')==e.symbol('active')==b'\0'
 assert not any(name.startswith('sb_cs1') for name in e.symbols)
base,binary=candidate.flash();assert base==0x10000000 and binary==(a.candidate/'springboard_rom_msc.bin').read_bytes()
uf2=(a.candidate/'springboard_rom_msc.uf2').read_bytes();prior=(a.baseline/'springboard_rom_msc.uf2').read_bytes()
assert uf2[:512]==prior[:512] and len(uf2)%512==0
first=struct.unpack_from('<8I',uf2);assert first[2:4]==(0xa000,0x10ffff00) and first[7]==0xe48bff57
pages=uf2[512:];count=len(pages)//512;expected=binary+bytes((-len(binary))%256);assert count*256==len(expected)
for i in range(count):
 block=pages[i*512:(i+1)*512];m0,m1,flags,addr,n,num,total,family=struct.unpack_from('<8I',block)
 assert (m0,m1,flags,n,num,total,family)==(0x0a324655,0x9e5d5157,0x2000,256,i,count,0xe48bff59)
 assert addr==base+i*256 and struct.unpack_from('<I',block,508)[0]==0x0ab16f30
 assert block[32:288]==expected[i*256:(i+1)*256]
def initialize(e):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
 for address,size in ((0x10000000,0x1000000),(0x20000000,0x100000),(0x40000000,0x50000),(0x50000000,0x400000)):u.mem_map(address,size)
 for section in e.sections:
  if section[1]!=8 and section[2]&2 and section[5]:u.mem_write(section[3],e.b[section[4]:section[4]+section[5]])
 u.mem_write(0x40020008,struct.pack('<I',0xffffffff));writes=[]
 def write(u,access,address,size,value,_):
  if address<0x20000000 or address>=0x40000000:writes.append([address,size,value])
 u.hook_add(UC_HOOK_MEM_WRITE,write)
 u.reg_write(UC_ARM_REG_R0,1);u.reg_write(UC_ARM_REG_SP,0x200ff000);u.reg_write(UC_ARM_REG_LR,0x200fe001)
 u.emu_start(e.symbols['sb_pio_rearm_program_init'][0]|1,0x200fe000,count=100000)
 assert u.reg_read(UC_ARM_REG_PC)==0x200fe000,'initializer failed to return'
 return writes
old,new=initialize(baseline),initialize(candidate)
def program(writes):return {((addr-0x50300048)//4):value for addr,size,value in writes if 0x50300048<=addr<0x503000c8}
op,np=program(old),program(new);assert len(op)==32 and len(np)==29 and set(np)==set(range(29))
assert all(op[i]==np[i] for i in range(23)), 'owner instruction changed'
assert [np[i] for i in range(23,29)]==[op[25],op[26],op[27],op[24],op[30],24], 'unexpected compacted guard instructions'
allowed={0x503000cc+0x18*sm for sm in (1,2,3)}|{0x503000d8+0x18*3}
def stable(writes):return [row for row in writes if not 0x50300048<=row[0]<0x503000c8 and row[0] not in allowed]
assert stable(old)==stable(new),'unexpected GPIO/PIO0/main/clock/FIFO configuration change'
for sm in (1,2,3):
 address=0x503000cc+0x18*sm
 prev=[value for addr,size,value in old if addr==address][-1];now=[value for addr,size,value in new if addr==address][-1]
 assert (prev & ~((31<<7)|(31<<12)))==(now & ~((31<<7)|(31<<12)))
 assert ((now>>7)&31,(now>>12)&31)==((27,25) if sm==3 else (23,26))
assert [value for addr,size,value in new if addr==0x50300120][-1]==27
report=dict(result='PASS',uf2_sha256=sha(uf2),elf_sha256=sha(candidate.b),bin_sha256=sha(binary),baseline_uf2_sha256=sha(prior),kernel_sha256=sha(candidate.symbol('sb_persistent_reply')),kernel_bytes=len(candidate.symbol('sb_persistent_reply')),owner_words_identical=23,baseline_words=op,candidate_words=np,freed_words=[29,30,31],baseline_writes=old,candidate_writes=new,unchanged_nonwatcher_configuration=True,embedded_rom_sha256=sha(a.recovery.read_bytes()),baseline_words_address=baseline.symbols['words'][0],candidate_words_address=candidate.symbols['words'][0],baseline_kernel_address=baseline.symbols['sb_persistent_reply'][0],candidate_kernel_address=candidate.symbols['sb_persistent_reply'][0],scope='actual linked initializer, ELF/BIN/UF2 and SRAM; no physical bus timing claim')
a.output.write_text(json.dumps(report,indent=2)+'\n');print('PASS actual linked initializer, owner23 words, pads/PIO0/SM0 unchanged, 3 free words, exact ARM kernel and ROM, ELF/BIN/UF2')
