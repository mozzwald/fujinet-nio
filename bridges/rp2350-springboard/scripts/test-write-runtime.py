#!/usr/bin/env python3
"""Actual linked Core0 arm/finish/status, mapped registers and FIFO fixtures.
No device access; PIO execution is tested separately with EPIO. Real snprintf,
validator, initializer and SDK register operations execute. Atomic/W1C registers
and preloaded capture FIFOs are modeled explicitly, not Unicorn mmio callbacks.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import *

p = argparse.ArgumentParser(); p.add_argument('elf', type=Path); p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
with a.elf.open('rb') as f:
    elf = ELFFile(f)
    symbols = {x.name: x['st_value'] for x in elf.get_section_by_name('.symtab').iter_symbols()}
    segments = [(s['p_vaddr'], s.data()) for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS); u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
for at, n in ((0x10000000, 0x1000000), (0x20000000, 0x100000), (0x40000000, 0x2000000),
              (0x50000000, 0x500000), (0xd0000000, 0x1000), (0xe000e000, 0x2000)):
    u.mem_map(at, n)
for at, raw in segments:
    if raw: u.mem_write(at, raw)
RET, STACK, CMD, OUT = 0x2007f100, 0x2007e000, 0x20070000, 0x20070800
queues = {(0, 3): [], (2, 0): [], (2, 1): [], (2, 3): []}
pending, writes, cases = [], [], []
now, pulses = 1000, 1
lose_idle_on_init = False

def put(at, n): u.mem_write(at, struct.pack('<I', n & 0xffffffff))
def get(at): return int.from_bytes(u.mem_read(at, 4), 'little')
put(0xd0000004, 15 << 16); put(0xd0000008, 1 << 13)
put(0x40020008, 0xffffffff)
put(0x50200000, 7); put(0x50300000, 15)

def code(uc, at, size, _):
    while pending:
        addr, value = pending.pop(0); put(addr, value)
    if at == symbols['time_us_64'] & ~1:
        uc.reg_write(UC_ARM_REG_R0, now & 0xffffffff); uc.reg_write(UC_ARM_REG_R1, now >> 32)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

def read(uc, access, at, size, value, _):
    if at in (0x5020000c, 0x5040000c):
        block = 0 if at == 0x5020000c else 2
        put(at, sum(len(q) << (sm * 8 + 4) for (b, sm), q in queues.items() if b == block))
    for block, base in ((0, 0x50200000), (2, 0x50400000)):
        if base + 0x20 <= at < base + 0x30:
            key = (block, (at - base - 0x20) // 4)
            assert key in queues and queues[key], ('unowned FIFO read', key)
            put(at, queues[key].pop(0))

def write(uc, access, at, size, value, _):
    global lose_idle_on_init
    if 0x40000000 <= at < 0x60000000: writes.append((at, value))
    # Atomic register aliases stage changes before the next ARM instruction.
    if 0x50200000 <= at < 0x50500000 and at & 0x3000:
        mode = (at >> 12) & 3; base = at & ~0x3000; prior = get(base)
        pending.append((base, prior ^ value if mode == 1 else prior | value if mode == 2 else prior & ~value))
    if at in (0x50200008, 0x50400008, 0x50200030, 0x50400030) or 0x40028230 <= at < 0x40028248:
        pending.append((at, get(at) & ~value))
    if at == 0x50400118: queues[2, 3] = []  # SDK FIFO clear through FJOIN toggling
    if at == 0x50400120 and value == 0x8020: queues[2, 3] = [(-pulses) & 0xffffffff]
    if at == 0x40028244 and lose_idle_on_init:
        put(0xd0000004, 14 << 16); lose_idle_on_init = False

u.hook_add(UC_HOOK_CODE, code)
# Scope hooks strictly to the mapped registers. Unicorn2.1.4 changes plain-RAM
# printf execution with overlapping global read/write hooks, even when both
# callbacks are no-ops. A separately retained plain-RAM control isolates this
# backend defect; do not change firmware or trust that rejected run.
u.hook_add(UC_HOOK_MEM_READ, read, begin=0x50200000, end=0x504fffff)
u.hook_add(UC_HOOK_MEM_WRITE, write, begin=0x40000000, end=0x5fffffff)

# Seed the real preserved read initializer, then audit capture operations
# against the resulting owned instruction/configuration bytes.
u.reg_write(UC_ARM_REG_R0, 1); u.reg_write(UC_ARM_REG_SP, STACK); u.reg_write(UC_ARM_REG_LR, RET | 1)
u.emu_start(symbols['sb_pio_rearm_program_init'] | 1, RET, count=100000)
assert u.reg_read(UC_ARM_REG_PC) == RET
owned_ranges = [(0x50200048, 28), (0x502000c8, 0x48), (0x50300000, 0x200), (0x40028000, 0x230)]
owned_bytes = [bytes(u.mem_read(at, n)) for at, n in owned_ranges]
writes.clear()

def call(command, ready=1):
    u.mem_write(CMD, command.encode() + b'\0'); u.mem_write(OUT, b'\0' * 512 + b'\xa5' * 16)
    u.reg_write(UC_ARM_REG_SP, STACK); u.reg_write(UC_ARM_REG_LR, RET | 1)
    for reg, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), (CMD, OUT, 512, ready)):
        u.reg_write(reg, v)
    u.emu_start(symbols['sb_write_capture_console'] | 1, RET, count=1000000)
    assert u.reg_read(UC_ARM_REG_PC) == RET, ('console did not return', command)
    assert bytes(u.mem_read(OUT + 512, 16)) == b'\xa5' * 16, 'status buffer overrun'
    return json.loads(bytes(u.mem_read(OUT, 512)).split(b'\0')[0])

def clear_frames():
    for k in queues: queues[k] = []

def load_frames(low_end=15, high_end=15, outcome=0, value=0x1234):
    queues[2, 0] = [5 << 16, low_end]
    queues[0, 3] = [(1 << 29) | (value << 4) | 5, high_end]
    queues[2, 1] = [outcome]

initial = call('write-status'); assert initial.get('phase') == 0, initial
assert not writes
assert call('write-arm', ready=0)['error'] == 'bus-not-ready'; assert not writes
put(0xd0000008, 0)
assert call('write-arm')['error'] == 'bus-not-ready'; assert not writes
put(0xd0000008, 1 << 13)
armed = call('write-arm'); assert armed['phase'] == 1 and armed['session'] == 1
assert get(0x50200000) & 15 == 15 and get(0x50300000) & 15 == 15
assert call('write-arm')['error'] == 'already-armed'
load_frames(); done = call('write-finish'); assert done['qualified'] == 1 and done['value'] == 0x1234
assert get(0x50200000) & 15 == 7 and get(0x50300000) & 15 == 15
assert call('write-status')['qualified'] == 1
assert call('write-finish')['error'] == 'not-armed'
cases += ['disarmed-readonly', 'not-ready', 'held-reset', 'arm-retains-read-guards',
          'double-arm-reject', 'actual-finish-and-formatter', 'stop-only-input-SMs', 'retained-result']
lose_idle_on_init = True
rejected = call('write-arm')
assert rejected['error'] == 'bus-not-ready' and rejected['session'] == 1 and rejected['value'] == 0x1234
assert get(0x50200000) & 15 == 7 and get(0x50400000) & 15 == 0
put(0xd0000004, 15 << 16); cases.append('idle-lost-during-init-keeps-saved-record')

for name, kind in [('cs-early', 0), ('missing-half', 1), ('extra-pulses', 2),
                   ('reset-history', 3), ('CS0-history', 4), ('OE-history', 5),
                   ('expired', 6), ('fault-during-capture', 7)]:
    clear_frames(); now += 100000; pulses = 1
    assert call('write-arm')['phase'] == 1
    load_frames()
    if kind == 0: queues[2, 1] = [0xffffffff]
    if kind == 1: queues[0, 3] = []
    if kind == 2: pulses = 2
    if kind == 3: put(0x40028244, 4 << 20)
    if kind == 4: put(0x40028238, 4)
    if kind == 5: put(0x40028238, 4 << 8)
    if kind == 6: now += 60000001
    result = call('write-finish', ready=kind != 7)
    assert result['qualified'] == 0, (name, result)
    cases.append(name)

assert not any(0x50300000 <= addr < 0x50400000 for addr, _ in writes), 'PIO1 changed'
assert [bytes(u.mem_read(at, n)) for at, n in owned_ranges] == owned_bytes, 'owned read config changed'
allowed0 = lambda addr: addr in (0x50200000, 0x50202000, 0x50203000, 0x50200008, 0x50200030, 0x50201118) or 0x50200064 <= addr < 0x502000b8 or 0x50200110 <= addr <= 0x50200124
assert all(value == 0x80000000 for addr, value in writes if addr == 0x50201118), 'unexpected SM3 FIFO XOR'
assert all(allowed0(addr) for addr, _ in writes if 0x50200000 <= addr < 0x50300000), ('unexpected PIO0 writes',[(hex(addr),hex(value)) for addr,value in writes if 0x50200000 <=addr<0x50300000 and not allowed0(addr)])
assert not any(0x40028000 <= addr < 0x40028230 or 0x40028248 <= addr < 0x40040000 for addr, _ in writes), 'GPIO pad/mux/IRQ enable modified'
maps = []
for base, first, count in ((0x50200000, 7, 21), (0x50400000, 0, 32)):
    words = {((at-base-0x48)//4): value for at, value in writes if base+0x48+4*first <= at < base+0x48+4*(first+count)}
    assert len(words) == count
    for word in words.values():
        major, dest = word >> 13, (word >> 5) & 7
        assert major in (0, 1, 2, 3, 4, 5, 6, 7)
        if major == 3: assert dest in (1, 2, 3), 'OUT can target only scratch/null'
        if major == 4: assert word == 0x8020, 'only PUSH BLOCK'
        if major == 5: assert dest in (2, 6, 7), 'MOV can target only Y/ISR/OSR'
        if major == 6: assert not word & 0x18, 'no cross-PIO signaling'
        if major == 7: assert dest in (1, 2), 'SET can target only X/Y'
    maps.append(words)
report = dict(result='PASS', cases=cases, elf_sha256=hashlib.sha256(a.elf.read_bytes()).hexdigest(),
              no_pio1_writes=True, original_pio0_SM_configuration_preserved=True,
              actual_input_only_programs=maps,
              scope='actual linked console/validator/initializer/SDK/formatter; mapped MMIO, FIFO fixtures and time mocked, no physical timing proof')
a.output.write_text(json.dumps(report, indent=2) + '\n'); print('PASS', len(cases), 'actual ARM write-management cases')
