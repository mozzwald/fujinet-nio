#!/usr/bin/env python3
"""Independent bank-A-specific ELF/BIN/UF2/SRAM audit; no hardware access."""
import argparse,importlib.util,json,struct,re,hashlib,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('directory',type=Path);p.add_argument('--reference',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2];workspace=root.parents[2]
spec=importlib.util.spec_from_file_location('elfcheck',workspace/'fujinet-palm/tools/verify-ramsetup-firmware.py');v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
def sha(b):return hashlib.sha256(b).hexdigest()
d=a.directory;e=v.ELF((d/'bank-a-ro.elf').read_bytes());old=v.ELF((workspace/'handspring-visor-plan/evidence/rom-management/2026-10-27-msc-003/rom-drive.elf').read_bytes());ref=v.ELF(a.reference.read_bytes())
base,binary=e.flash();assert base==0x10000000 and binary==(d/'bank-a-ro.bin').read_bytes()
uf=(d/'bank-a-ro.uf2').read_bytes();baseline=(workspace/'handspring-visor-plan/evidence/rom-management/2026-10-27-msc-003/rom-drive.uf2').read_bytes()
assert sha(baseline)=='3005edefaf45cbf3ee180dd464f0cc3bcf936c7c6e54ae25a6963ee4b720167b'
assert uf[:512]==baseline[:512] and len(uf)%512==0
payload=binary+bytes((-len(binary))%256);count=len(uf)//512-1;assert count*256==len(payload)
seen=set()
for i in range(count):
 b=uf[512*(i+1):512*(i+2)];m0,m1,flags,address,n,index,total,family=struct.unpack_from('<8I',b)
 assert (m0,m1,flags,n,total,family)==(0x0a324655,0x9e5d5157,0x2000,256,count,0xe48bff59)
 assert index not in seen and index<count and address==base+256*index;seen.add(index)
 assert b[32:288]==payload[256*index:256*(index+1)] and struct.unpack_from('<I',b,508)[0]==0x0ab16f30
assert e.symbol('sb_persistent_reply')==ref.symbol('sb_persistent_reply'),'integrated ARM differs from Experiment A'
assert e.symbol('sb_boot_autoarm_entry')==old.symbol('sb_boot_autoarm_entry'),'boot shim instruction change'
assert e.symbol('sb_rom_image')==old.symbol('sb_rom_image'),'recovery payload changed'
assert e.symbol('device')==old.symbol('device') and e.symbol('config')==old.symbol('config'),'USB descriptors changed'
sy={k:x[0] for k,x in e.symbols.items()}
for row in subprocess.check_output(['arm-none-eabi-nm','-n',str(d/'bank-a-ro.elf')],text=True).splitlines():
 cols=row.split()
 if len(cols)==3:sy.setdefault(cols[2],int(cols[0],16))
expected={'__msc_start':0x10100000,'__msc_end':0x10d00000,'__rom_slot0':0x10fc0000,'__rom_slot1':0x10fd1000,'__rom_metadata0':0x10fe2000,'__rom_metadata1':0x10fe3000,'__rom_storage_end':0x10fe4000}
assert all(sy[k]==n for k,n in expected.items());assert sy['__flash_binary_end']<=0x10100000
assert e.symbols['lookup_banks'][1]==131072 and e.symbols['words'][1]==e.symbols['registers'][1]==65536
assert sy['lookup_banks']==sy['registers'] and sy['words']==sy['registers']+65536
assert sy['words']%16==sy['registers']%16==8
buffers={}
for name in ('sb_lookup_backing','bytes','import_buffer','disk','storage'):
 addr,n,_,_=e.symbols[name];assert 0x20000000<=addr<addr+n<=sy['__bss_end__'];buffers[name]={'address':hex(addr),'size':n}
for k,x in buffers.items():
 for j,y in buffers.items():
  if k<j:assert int(x['address'],16)+x['size']<=int(y['address'],16) or int(y['address'],16)+y['size']<=int(x['address'],16)
assert sy['__bss_end__']<sy['__StackLimit']<=sy['__StackOneBottom']<sy['__StackOneTop']<=sy['__StackBottom']<sy['__StackTop']<=0x20082000
assert sy['__StackOneTop']-sy['__StackOneBottom']==sy['__StackTop']-sy['__StackBottom']==4096
allowed=re.compile(r'^(_entry_point|_reset_handler|__unhandled_user_irq|isr_.*)$');stubs=[]
for name,(addr,n,kind,index) in e.symbols.items():
 if kind==2 and 0x10000000<=addr<0x11000000:assert allowed.fullmatch(name),(name,hex(addr));stubs.append(name)
for name in ('service','sb_persistent_reply','sb_pio_rearm_program_init','sb_boot_autoarm_entry','sb_msc_fault_park','initialize_usb','install_ram_exceptions','flash_range_program','flash_range_erase','tud_task_ext'):
 assert 0x20000000<=sy[name]<0x20082000,(name,hex(sy[name]))
assert sy['isr_hardfault']==sy['isr_nmi']==sy['sb_msc_fault_park']
for name in ('writes','sb_stage_a_retire','sb_stage_a_fault','sb_async_reply','stdio_usb_init'):assert name not in sy,name
assert e.symbol('arm')==e.symbol('active')==b'\0' and e.symbol('stopped')==b'\1'
policy=struct.unpack('<4I',e.symbol('sb_boot_policy'));assert policy==(sy['rom_ready'],sy['arm'],sy['service'],0)
assert b'cs1_writes\\\":false' in (d/'bank-a-ro.elf').read_bytes() or b'cs1_writes":false' in (d/'bank-a-ro.elf').read_bytes()
report=dict(result='PASS',scope='integrated bank-A linkage/placement, not physical timing or complete dynamic stack proof',hashes={n:sha((d/n).read_bytes()) for n in ('bank-a-ro.elf','bank-a-ro.bin','bank-a-ro.uf2')},arm_Experiment_A_identical=True,arm_sha256=sha(e.symbol('sb_persistent_reply')),boot_shim_identical=True,recovery_ROM_identical=True,USB_descriptors_identical=True,ROM_words=hex(sy['words']),CS1_bank=hex(sy['registers']),buffers=buffers,bss_end=hex(sy['__bss_end__']),heap_limit=hex(sy['__StackLimit']),remaining_below_heap_limit=sy['__StackLimit']-sy['__bss_end__'],core0_stack=4096,core1_stack=4096,partitions=expected,flash_end=hex(sy['__flash_binary_end']),default_XIP_stubs_repaired_by_runtime=stubs,cs1_writes=False,rollback_UF2_SHA256=sha(baseline),hardware_verified=False)
a.output.write_text(json.dumps(report,indent=2)+'\n');print('PASS bank-A ELF/BIN/UF2/ROM/USB/boot/SRAM linkage')
