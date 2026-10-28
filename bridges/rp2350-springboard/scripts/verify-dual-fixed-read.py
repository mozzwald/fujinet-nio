#!/usr/bin/env python3
"""Independent container checks and actual linked initializer differential.

Specifically audits this 32-word fixed-read layout; does not relax the frozen MSC verifier.
No hardware access. Peripheral reset completion is modeled as already released.
"""
import argparse,hashlib,importlib.util,json,struct,subprocess
from pathlib import Path
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_MEM_WRITE
from unicorn.arm_const import *
p=argparse.ArgumentParser();p.add_argument('baseline',type=Path);p.add_argument('candidate',type=Path);p.add_argument('recovery',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--host-elf',type=Path,required=True);a=p.parse_args()
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
host=v.ELF(a.host_elf.read_bytes())
assert candidate.symbol('sb_persistent_reply')==host.symbol('sb_persistent_reply'),'linked ARM differs from tested host worker'
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
op,np=program(old),program(new)
assert len(op)==29 and len(np)==32 and set(np)==set(range(32))
# Exact protected operations, with only declared label relocations/autopush.
for dest,src in [(0,0),(1,1),(3,3),(4,4),(5,6),(6,7),(7,8),(8,9),(12,13),(13,14),(15,16),(16,17),(17,18),(18,19)]:assert np[dest]==op[src],(dest,src)
for dest,src,target in [(2,2,29),(9,10,15),(10,11,12),(11,12,15),(14,15,17)]:assert np[dest]==(op[src]&~31)|target,(dest,src)
assert [np[i] for i in (19,20,21,22,23,24,25,26,27,28,29,30,31)]==[0x4020,0xe04a,0xd9,0x20a0,op[24],op[25],0x2081,23,op[27],23,0xe049,0xb4,3]
def global_writes(w):return [row for row in w if not 0x50200000<=row[0]<0x50400000]
assert global_writes(old)==global_writes(new),'GPIO/pads/clock/reset setup changed'
def last(w,addr):return [value for address,size,value in w if address==addr][-1]
old0={((addr-0x50200048)//4):value for addr,size,value in old if 0x50200048<=addr<0x502000c8}
new0={((addr-0x50200048)//4):value for addr,size,value in new if 0x50200048<=addr<0x502000c8}
assert len(new0)==7 and [new0[i] for i in range(3)]==[old0[i] for i in range(3)]
assert [new0[i] for i in range(3,7)]==[old0[0],0xc6,old0[1],old0[2]]
assert last(new,0x503000d0)==(last(old,0x503000d0)|(1<<16)), 'only autopush added to shift config'
for sm in range(4):
 assert last(new,0x503000c8+0x18*sm)==last(old,0x503000c8+0x18*sm)
 assert last(new,0x503000dc+0x18*sm)==last(old,0x503000dc+0x18*sm)
 now=last(new,0x503000cc+0x18*sm)
 assert ((now>>7)&31,(now>>12)&31)==[(0,20),(21,24),(22,24),(27,24)][sm]
 assert (now & ~((31<<7)|(31<<12)))==(last(old,0x503000cc+0x18*sm)&~((31<<7)|(31<<12)))
assert last(new,0x50300000)&15==15 and last(new,0x50200000)&15==7
for address in (0x50200168,0x50300168):assert last(new,address)==last(old,address)==16
for sm,entry in enumerate((0,21,22,27)):assert last(new,0x503000d8+sm*0x18)==entry
for sm,entry in enumerate((0,3,3)):assert last(new,0x502000d8+sm*0x18)==entry
report=dict(result='PASS',uf2_sha256=sha(uf2),elf_sha256=sha(candidate.b),bin_sha256=sha(binary),baseline_uf2_sha256=sha(prior),kernel_sha256=sha(candidate.symbol('sb_persistent_reply')),kernel_bytes=len(candidate.symbol('sb_persistent_reply')),baseline_words=op,candidate_words=np,pio0_words=new0,all_32_words_occupied=True,baseline_writes=old,candidate_writes=new,unchanged_gpio_pads_and_clock=True,embedded_rom_sha256=sha(a.recovery.read_bytes()),baseline_words_address=baseline.symbols['words'][0],candidate_words_address=candidate.symbols['words'][0],baseline_kernel_address=baseline.symbols['sb_persistent_reply'][0],candidate_kernel_address=candidate.symbols['sb_persistent_reply'][0],scope='actual linked initializer, ELF/BIN/UF2 and SRAM; no physical bus timing claim')
a.output.write_text(json.dumps(report,indent=2)+'\n');print('PASS actual linked 32-word layout, GPIO/pads unchanged, tested ARM worker, ROM/USB/partitions and ELF/BIN/UF2')
