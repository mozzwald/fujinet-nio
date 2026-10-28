#!/usr/bin/env python3
"""Plain-RAM control for the rejected global memory-hook backend. Offline only."""
import argparse
import json
from elftools.elf.elffile import ELFFile
from unicorn import *
from unicorn.arm_const import *

p = argparse.ArgumentParser(); p.add_argument('elf'); p.add_argument('--output', required=True); a = p.parse_args()
with open(a.elf, 'rb') as f:
    e = ELFFile(f); symbols = {x.name: x['st_value'] for x in e.get_section_by_name('.symtab').iter_symbols()}
    segments = [(s['p_vaddr'], s.data()) for s in e.iter_segments() if s['p_type'] == 'PT_LOAD']
results = {}
for mode in ('plain', 'mmio-only', 'global-rejected'):
    u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS); u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
    for at, size in ((0x10000000, 0x1000000), (0x20000000, 0x100000)):
        u.mem_map(at, size)
    for at, raw in segments:
        if raw: u.mem_write(at, raw)
    if mode != 'plain':
        where = dict(begin=0x40000000, end=0x5fffffff) if mode == 'mmio-only' else {}
        u.hook_add(UC_HOOK_MEM_READ, lambda *args: None, **where)
        u.hook_add(UC_HOOK_MEM_WRITE, lambda *args: None, **where)
    u.mem_write(0x20070000, b'hello\0')
    for r, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
                     UC_ARM_REG_SP, UC_ARM_REG_LR),
                    (0x20070800, 512, 0x20070000, 7, 0x2007e000, 0x2007f101)):
        u.reg_write(r, v)
    u.emu_start(symbols['__wrap_snprintf'] | 1, 0x2007f100, count=1000000)
    results[mode] = dict(return_value=u.reg_read(UC_ARM_REG_R0),
                         output=bytes(u.mem_read(0x20070800, 32)).split(b'\0')[0].decode())
assert results['plain'] == results['mmio-only'] == dict(return_value=5, output='hello')
results['global_backend_defect_reproduced'] = results['global-rejected'] != results['plain']
with open(a.output, 'w') as f: json.dump(results, f, indent=2); f.write('\n')
print(json.dumps(results, indent=2))
