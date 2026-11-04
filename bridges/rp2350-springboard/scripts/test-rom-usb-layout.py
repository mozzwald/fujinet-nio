#!/usr/bin/env python3
"""Exercise actual GNU ld partition assertions, including negative layouts."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'empty.S').write_text('.syntax unified\n.thumb\n.section .text\nnop\n')
 subprocess.run(['arm-none-eabi-gcc','-mcpu=cortex-m33','-mthumb','-c',str(p/'empty.S'),'-o',str(p/'empty.o')],check=True)
 for capacity,end,expected in ((0x1000000,0x10010000,True),(0x1000000,0x10100001,False),(0x800000,0x10010000,False)):
  (p/'layout.ld').write_text(f'MEMORY {{ FLASH (rx) : ORIGIN = 0x10000000, LENGTH = {capacity} }}\nSECTIONS {{ .text : {{ *(.text) }} > FLASH }}\n__flash_binary_end = {end};\n')
  r=subprocess.run(['arm-none-eabi-ld','-T',str(p/'layout.ld'),'-T',str(ROOT/'rom-management/partitions.ld'),str(p/'empty.o'),'-o',str(p/'result.elf')],capture_output=True,text=True)
  assert (r.returncode==0)==expected,(r.stderr,capacity,end)
  if not expected:assert 'Firmware exceeds' in r.stderr or '16MiB flash' in r.stderr,r.stderr
print('PASS: real linker accepts bounded16MiB layout; rejects oversized firmware and8MiB flash')
