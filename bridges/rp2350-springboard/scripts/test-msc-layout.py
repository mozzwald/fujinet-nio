#!/usr/bin/env python3
"""Check the actual GNU ld MSC partition assertions, without hardware."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp);(p/'empty.S').write_text('.syntax unified\n.thumb\n.section .text\nnop\n')
 subprocess.run(['arm-none-eabi-gcc','-mcpu=cortex-m33','-mthumb','-c',str(p/'empty.S'),'-o',str(p/'empty.o')],check=True)
 for capacity,end,expected in ((0x1000000,0x10010000,True),(0x1000000,0x10100000,True),(0x1000000,0x10100001,False),(0x800000,0x10010000,False)):
  (p/'layout.ld').write_text(f'MEMORY {{ FLASH (rx) : ORIGIN = 0x10000000, LENGTH = {capacity} }}\nSECTIONS {{ .text : {{ *(.text) }} > FLASH }}\n__flash_binary_end = {end};\n')
  r=subprocess.run(['arm-none-eabi-ld','-T',str(p/'layout.ld'),'-T',str(ROOT/'msc-rom/partitions.ld'),str(p/'empty.o'),'-o',str(p/'out.elf')],capture_output=True,text=True)
  assert (r.returncode==0)==expected,(capacity,end,r.stderr)
  if not expected:assert '1MiB' in r.stderr or '16MiB' in r.stderr,r.stderr
print('PASS actual ld bounds: normal/exact-limit images accepted; firmware overflow and 8MiB device rejected')
