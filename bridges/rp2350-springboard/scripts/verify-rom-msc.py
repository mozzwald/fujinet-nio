#!/usr/bin/env python3
"""Independent ELF/BIN/UF2 linkage, frozen bus engine and SRAM/XIP audit."""
import argparse,hashlib,importlib.util,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for key in ('elf','bin','uf2','baseline-elf','baseline-uf2','recovery','output'):
    p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args()
vpath=Path(__file__).resolve().parents[4]/'fujinet-palm/tools/verify-ramsetup-firmware.py'
spec=importlib.util.spec_from_file_location('verify',vpath);v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
r=v.verify(a.elf.read_bytes(),a.bin.read_bytes(),a.uf2.read_bytes(),a.baseline_elf.read_bytes(),a.baseline_uf2.read_bytes(),a.recovery.read_bytes())
e=v.ELF(a.elf.read_bytes());nm=subprocess.check_output(['arm-none-eabi-nm','-n',str(a.elf)],text=True)
sym={s.split()[-1]:int(s.split()[0],16) for s in nm.splitlines() if len(s.split())==3}
expected={'__msc_start':0x10100000,'__msc_end':0x10d00000,'__rom_slot0':0x10fc0000,'__rom_slot1':0x10fd1000,'__rom_metadata0':0x10fe2000,'__rom_metadata1':0x10fe3000,'__rom_storage_end':0x10fe4000}
for name,n in expected.items():assert sym[name]==n,name
assert sym['__flash_binary_end']<=0x10100000
for name in ('service','sb_msc_fault_park','isr_hardfault','isr_nmi','flash_range_erase','flash_range_program','install_ram_exceptions','er','pg','safe','rd','ms_write','mi_load','mi_promote','tud_task_ext','dcd_rp2040_irq','ram_vector_table'):
    assert 0x20000000<=sym[name]<0x20082000,(name,hex(sym[name]))
assert sym['isr_hardfault']==sym['isr_nmi']==sym['sb_msc_fault_park']
# Every stored normal function is RAM-resident. The exceptions below belong to
# SDK flash boot/default vector stubs; all default vectors are replaced before
# live programming. They are not permission for a Core1 branch into XIP.
xip=[]
allowed=re.compile(r'^(_entry_point|_reset_handler|__unhandled_user_irq|isr_.*)$')
for name,(addr,n,kind,index) in e.symbols.items():
    if kind==2 and 0x10000000<=addr<0x11000000:
        assert allowed.fullmatch(name),(name,hex(addr));xip.append(name)
for name in ('bytes','words','import_buffer','disk','storage'):
    addr,n,_,_=e.symbols[name];assert 0x20000000<=addr<=addr+n<=0x20080000
assert sym['words']%16==8
assert e.symbol('boot_phase')==b'\1'
assert e.symbol('ram_island_ok')==e.symbol('flash_size_ok')==b'\0'
assert b'FujiNet ROM Drive v1\0' in a.elf.read_bytes()
assert 'stdio_usb_init' not in sym and 'low_priority_worker_irq' not in sym
assert sym['__StackTop']-sym['__StackBottom']==4096
assert sym['__StackOneTop']-sym['__StackOneBottom']==4096
# Decode actual linked descriptors: CDC IAD+2 interfaces and MSC, noncolliding EPs.
descriptor=e.symbol('device');assert descriptor[4:7]==b'\xef\x02\x01'
config=e.symbol('config');assert config[4]==3
interfaces=[];eps=[];i=0
while i<len(config):
    n=config[i];assert n>=2 and i+n<=len(config)
    if config[i+1]==4:interfaces.append((config[i+2],config[i+5]))
    if config[i+1]==5:eps.append(config[i+2])
    i+=n
assert interfaces==[(0,2),(1,10),(2,8)],interfaces
assert sorted(eps)==[2,3,129,130,131],eps
r.update(partitions=expected,firmware_end=sym['__flash_binary_end'],bss_end=sym['__bss_end__'],
         remaining_sram_below_heap_limit=sym['__StackLimit']-sym['__bss_end__'],
         core0_stack_bytes=4096,core1_stack_bytes=4096,
         fault_nmi_in_sram=True,default_xip_stubs_replaced_at_runtime=xip,
         usb_interfaces=interfaces,usb_endpoints=eps,usb_vid=int.from_bytes(descriptor[8:10],'little'),
         usb_pid=int.from_bytes(descriptor[10:12],'little'),usb_background_stdio_disabled=True,
         startup_import_only=True,physical_concurrent_writes_qualified=False)
a.output.write_text(json.dumps(r,indent=2)+'\n');print('PASS frozen kernel/PIO, ELF/BIN/UF2, partitions, SRAM/fault placement and composite descriptors')
