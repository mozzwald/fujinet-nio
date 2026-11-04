#!/usr/bin/env python3
"""Independent CS1+MSC ELF/BIN/UF2, unchanged PIO/ARM and SRAM audit."""
import argparse,hashlib,importlib.util,json,re,struct,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('elf','bin','uf2','recovery','reference','baseline-uf2','dis','output'):p.add_argument('--'+name,type=Path,required=True)
p.add_argument('--require-once-only-fault-polling',action='store_true',help='mandatory for the isolated H2 candidate')
a=p.parse_args();workspace=Path(__file__).resolve().parents[4]
vpath=workspace/'fujinet-palm/tools/verify-cs1module-firmware.py';spec=importlib.util.spec_from_file_location('verify',vpath);v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
r=v.verify(a.elf.read_bytes(),a.bin.read_bytes(),a.uf2.read_bytes(),a.recovery.read_bytes(),a.reference.read_bytes(),a.baseline_uf2.read_bytes(),a.reference.read_bytes(),a.dis.read_text())
e=v.module('stagefw','verify-cs1-stagea-firmware.py').m.ELF(a.elf.read_bytes());old=v.module('stagefw','verify-cs1-stagea-firmware.py').m.ELF(a.reference.read_bytes())
# Initializer bytes deliberately change to repair APIO scratch-buffer commits.
# Compare the actual linked hardware writes, not just an embedded read template.
pio_report=a.output.with_suffix('.pio-init.json')
subprocess.run([sys.executable,str(Path(__file__).with_name('test-cs1-pio-init.py')),
                str(a.elf),'--output',str(pio_report)],check=True)
r['compiled_initializer_matches_native_instruction_ram']=True
r['pio_instruction_words_checked']=91
if a.require_once_only_fault_polling:
 fault_report=a.output.with_suffix('.fault-polling.json')
 subprocess.run([sys.executable,str(Path(__file__).with_name('test-cs1-fault-polling.py')),
                 str(a.elf),'--output',str(fault_report)],check=True)
 r['once_only_fault_polling']=True

nm={x.split()[-1]:int(x.split()[0],16) for x in subprocess.check_output(['arm-none-eabi-nm','-n',str(a.elf)],text=True).splitlines() if len(x.split())==3}
expected={'__msc_start':0x10100000,'__msc_end':0x10d00000,'__rom_slot0':0x10fc0000,'__rom_slot1':0x10fd1000,'__rom_metadata0':0x10fe2000,'__rom_metadata1':0x10fe3000,'__rom_storage_end':0x10fe4000}
for name,n in expected.items():assert nm[name]==n
assert nm['__flash_binary_end']<=0x10100000
for name in ('core1','sb_async_reply','sb_async_program_init','initialize_usb','install_ram_exceptions','sb_msc_fault_park','isr_hardfault','isr_nmi','flash_range_erase','flash_range_program','writes','recover_step','recover_request','main','ms_write'):
 assert 0x20000000<=e.symbols[name][0]<0x20082000,(name,hex(e.symbols[name][0]))
allowed=re.compile(r'^(_entry_point|_reset_handler|__unhandled_user_irq|isr_.*)$')
for name,(address,n,kind,index) in e.symbols.items():
 if kind==2 and 0x10000000<=address<0x11000000:assert allowed.fullmatch(name),(name,hex(address))
assert e.symbols['words'][0]%16==8
for name in ('words','table','bytes'):
 address,n,_,_=e.symbols[name];assert n==65536 and 0x20000000<=address<address+n<=0x20080000
assert 'stdio_usb_init' not in e.symbols
config=e.symbol('config');device=e.symbol('device');assert config[4]==3 and int.from_bytes(device[8:10],'little')==0xcafe and int.from_bytes(device[10:12],'little')==0x4014
assert b'FujiNet CS1 ROM Drive A1\0' in a.elf.read_bytes()
# Recompute the build identity from the exact ordered CMake source inputs.
root=Path(__file__).resolve().parents[1]
identity='Release/waveshare_core2350b/rp2350-arm-s'
files='src/async_msc_main.c src/async_msc_descriptors.c src/async_reply.S src/async_program.c src/async_boot.c src/async_stage_a.c src/cs1_registers.c src/msc_store.c src/msc_image.c src/rom_store.c include/visor_mailbox.h msc-rom/tusb_config.h msc-rom/partitions.ld'.split()
for name in files:identity+='/'+name+':'+hashlib.sha256((root/name).read_bytes()).hexdigest()
identity+='/recovery:'+hashlib.sha256(a.recovery.read_bytes()).hexdigest()
assert hashlib.sha256(identity.encode()).hexdigest()==r['build_id'],'stale build identity'
r.update(partitions=expected,identity_recomputed=True,composite_usb=True,usb_irq_fix_present=True,
 flash_write_rom_concurrency_deferred=True,physical_scope_timing_qualified=False,
 remaining_sram=nm['__StackLimit']-nm['__bss_end__'])
a.output.write_text(json.dumps(r,indent=2)+'\n');print('PASS CS1+MSC provenance, source build identity, unchanged300-byte ARM, actual PIO instruction RAM, slots and SRAM')
