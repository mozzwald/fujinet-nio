#!/usr/bin/env python3
"""Verify candidate linkage, frozen engine, SRAM placement and partition symbols.

Reuses the established independent ELF/BIN/UF2 verifier in sibling fujinet-palm.
No USB/programmer access. All paths are explicit except the workspace verifier.
"""
import argparse,importlib.util,json,subprocess,zlib
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for key in ('elf','bin','uf2','baseline-elf','baseline-uf2','recovery','output'):p.add_argument('--'+key,type=Path,required=True)
p.add_argument('--verifier',type=Path,default=Path(__file__).resolve().parents[4]/'fujinet-palm/tools/verify-ramsetup-firmware.py')
a=p.parse_args();spec=importlib.util.spec_from_file_location('frozen',a.verifier);v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
r=v.verify(a.elf.read_bytes(),a.bin.read_bytes(),a.uf2.read_bytes(),a.baseline_elf.read_bytes(),a.baseline_uf2.read_bytes(),a.recovery.read_bytes());e=v.ELF(a.elf.read_bytes())
symbols={line.split()[-1]:int(line.split()[0],16) for line in subprocess.check_output(['arm-none-eabi-nm','-n',str(a.elf)],text=True).splitlines() if len(line.split())==3}
expected={'__rom_slot0':0x10fc0000,'__rom_slot1':0x10fd1000,'__rom_metadata0':0x10fe2000,'__rom_metadata1':0x10fe3000,'__rom_storage_end':0x10fe4000}
for name,value in expected.items():assert symbols[name]==value,(name,symbols[name])
assert symbols['__flash_binary_end']<=0x10100000
for name in ('bytes','words'):assert e.symbols[name][1]==65536 and 0x20000000<=symbols[name]<symbols[name]+65536<=0x20080000
for name in ('main','bus_safe','er','pg','quiesce','resume','load_active','rm_dispatch','rs_begin','rs_chunk','rs_finish','rs_activate','flash_range_erase','flash_range_program'):
 assert 0x20000000<=symbols[name]<0x20080000,(name,hex(symbols[name]))
assert b'FujiNet ROM Manager v1\0' in a.elf.read_bytes()
r.update(partition_symbols=expected,firmware_limit=0x10100000,firmware_end=symbols['__flash_binary_end'],bss_end=symbols['__bss_end__'],core0_stack_bytes=symbols['__StackTop']-symbols['__StackBottom'],core1_stack_bytes=symbols['__StackOneTop']-symbols['__StackOneBottom'],recovery_bytes=a.recovery.stat().st_size,recovery_crc32=f'{zlib.crc32(a.recovery.read_bytes()):08x}',management_flash_functions_in_sram=True,dedicated_usb_product=True,physical_programming_qualified=False)
a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
