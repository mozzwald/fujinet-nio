#!/usr/bin/env python3
"""Execute linked ARM machine code against the native PIO model.

Unicorn is functional, not cycle-accurate. Profiles below are explicit timing
assumptions for phase/stall testing; physical timing must be measured separately.
Requires unicorn and pyelftools; the shared helper is built by host CMake.
"""
import argparse
import ctypes as C
import json
from pathlib import Path
import re
import struct
import subprocess

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import *

RESET = 1 << 45
IDLE = RESET | (15 << 16)
READ = RESET | (10 << 16)
PIO, SIO = 0x50300000, 0xd0000004
TOTALS, BUS, STOP, STACK, RETURN = 0x200f0000, 0x200f0100, 0x200f1000, 0x200fd000, 0x200fe000


class Event(C.Structure):
    _fields_ = [(k, C.c_uint32) for k in ('clock', 'kind', 'owner', 'value')]


def word(addr):
    fixed = {8: 0xfeed, 10: 0xbeef, 0x444: 0xa174, 0x446: 0x5c8f, 0x100: 0, 0x102: 0xffff}
    return 0xffff if addr >= 65536 else fixed.get(addr, ((addr * 40503) ^ (addr >> 3) ^ 0xa55a) & 65535)


def pins(addr, controls):
    # PIO1 high window: A16..18 at36..38; A19..23 at40..44.
    return controls | (addr & 0xfffe) | (((addr >> 16) & 7) << 36) | (((addr >> 19) & 31) << 40)


class Runner:
    def __init__(self, elf, library):
        self.lib = C.CDLL(str(library.resolve()))
        for name in ('clock', 'fstat', 'gpio', 'events', 'driven', 'word'):
            getattr(self.lib, 'sb_cosim_' + name).restype = C.c_uint32
        self.lib.sb_cosim_bus.argtypes = [C.c_uint64, C.c_uint]
        self.lib.sb_cosim_rx.restype = C.c_uint64
        self.lib.sb_cosim_log.restype = C.POINTER(Event)
        with elf.open('rb') as stream:
            e = ELFFile(stream)
            self.segments = [(s['p_vaddr'], s.data()) for s in e.iter_segments()
                             if s['p_type'] == 'PT_LOAD' and 0x20000000 <= s['p_vaddr'] < 0x20100000]
            self.symbols = {s.name: s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
        dis = subprocess.check_output(['arm-none-eabi-objdump', '-d', str(elf)], text=True)
        self.lines = {int(m[1], 16): m[2] for m in re.finditer(
            r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{4}\s+)+(.+)$', dis, re.M)}

    def run(self, segments, *, profile=1, stall=None, corrupt=False, invalid=False):
        assert self.lib.sb_cosim_init() == 0
        u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
        for a, n in [(0x20000000, 0x100000)]:
            u.mem_map(a, n)
        for a, data in self.segments:
            u.mem_write(a, data)
        table = b''.join(struct.pack('<H', word(a)) for a in range(0, 65536, 2))
        u.mem_write(self.symbols['words'], table)
        u.mem_write(BUS, struct.pack('<III', PIO, SIO, STOP))
        u.mem_write(TOTALS, bytes(24))
        u.mem_write(STOP, b'\0')
        u.reg_write(UC_ARM_REG_R0, TOTALS)
        u.reg_write(UC_ARM_REG_R1, self.symbols['words'])
        u.reg_write(UC_ARM_REG_R2, BUS)
        u.reg_write(UC_ARM_REG_SP, STACK)
        u.reg_write(UC_ARM_REG_LR, RETURN | 1)
        saved = {reg: 0x12340000 + i for i, reg in enumerate(range(UC_ARM_REG_R4, UC_ARM_REG_R12))}
        for reg, value in saved.items():
            u.reg_write(reg, value)
        end = max(s[1] for s in segments) + 400
        events, trace = [], []
        owner, phase, returned, previous = 0, 1, False, None
        saved_high, expected_word, sent = 0, None, False

        def advance(clocks):
            for _ in range(clocks):
                t = self.lib.sb_cosim_clock()
                address, controls = 0, IDLE
                for start, stop, addr, ctrl in segments:
                    if start <= t < stop:
                        address, controls = addr, ctrl
                        break
                self.lib.sb_cosim_bus(pins(address, controls), address)
                error = self.lib.sb_cosim_step(1)
                assert not error, ('PIO safety', error, t, segments, profile, stall)
            if self.lib.sb_cosim_clock() >= end:
                u.mem_write(STOP, b'\1')

        def code(uc, address, size, _):
            nonlocal returned, previous
            if address == RETURN:
                returned = True
                uc.emu_stop()
                return
            if stall and stall[0] <= self.lib.sb_cosim_clock() < stall[1]:
                advance(stall[1] - self.lib.sb_cosim_clock())
            op = self.lines[address]
            mnemonic = op.split()[0]
            cost = 1
            if mnemonic.startswith(('ldr', 'str')):
                cost += profile - 1
            if mnemonic.startswith(('stm', 'ldm')):
                cost += 8  # Nine saved registers, once per arm/exit.
            if mnemonic.startswith(('b.', 'beq', 'bne', 'bmi', 'cb')):
                cost += profile - 1
            # Count condition-failed IT instructions which Unicorn does not
            # hook. Exclude actual branch discontinuities.
            if previous and not previous[2].startswith(('b', 'pop', 'ldm')):
                lo = previous[0] + previous[1]
                if lo < address <= lo + 12:
                    cost += sum(lo <= p < address for p in self.lines)
            advance(cost)
            trace.append(dict(clock=self.lib.sb_cosim_clock(), pc=f'{address:08x}', instruction=op))
            previous = address, size, mnemonic

        def read(uc, offset, size, _):
            address = PIO + offset
            nonlocal owner, phase, saved_high, sent
            if address == PIO + 4:
                return self.lib.sb_cosim_fstat()
            elif address == PIO + 0x20:
                tagged = self.lib.sb_cosim_rx()
                kind = (tagged >> 32) & 3
                assert kind == phase, ('RX ownership phase', kind, phase)
                new_owner = tagged >> 34
                if kind == 2:
                    assert new_owner == owner and sent, 'completion does not belong to posted request'
                owner = new_owner
                value = tagged & 0xffffffff
                if kind == 1:
                    phase = 2
                    sent = False
                    if invalid:
                        value &= ~0x20000000
                    saved_high = value
                else:
                    phase = 1
                    if corrupt:
                        value = 0x12345678
                events.append(dict(clock=self.lib.sb_cosim_clock(), event='request' if kind == 1 else 'completion', owner=owner, value=value, instruction_index=len(trace)))
                return value
            raise AssertionError(('unexpected PIO read', hex(address)))

        def sio_read(uc, offset, size, _):
            nonlocal expected_word
            assert offset == 4
            low = self.lib.sb_cosim_gpio()
            expected_word = 0xffff if saved_high & 0x1f700000 else word(low & 0xfffe)
            events.append(dict(clock=self.lib.sb_cosim_clock(), event='low-address', instruction_index=len(trace)))
            return low

        def write(uc, offset, size, value, _):
            nonlocal sent
            address = PIO + offset
            if address == PIO + 0x10:
                assert phase == 2 and not sent, 'TX without owned request or duplicate TX'
                sent = True
                valid = saved_high & 0x2000000f == 0x2000000a
                assert value == (expected_word if valid else 0), 'TX lookup or invalid dummy mismatch'
                self.lib.sb_cosim_tx(value, owner)
                events.append(dict(clock=self.lib.sb_cosim_clock(), event='TX', owner=owner, value=value, instruction_index=len(trace)))
            elif address == PIO + 0x34:
                assert value == 1, (hex(value), hex(uc.reg_read(UC_ARM_REG_R10)), hex(uc.reg_read(UC_ARM_REG_PC)), segments, trace[-8:])
                self.lib.sb_cosim_cancel()

        u.hook_add(UC_HOOK_CODE, code)
        # MMIO callbacks return values directly. Writing backing RAM from an
        # ordinary read hook corrupts Unicorn IT-state across a TB restart.
        u.mmio_map(PIO, 0x1000, read, None, write, None)
        u.mmio_map(0xd0000000, 0x1000, sio_read, None, None, None)
        u.emu_start(self.symbols['sb_persistent_reply'], RETURN + 2, count=100000)
        assert returned, 'ARM loop did not stop'
        totals = dict(zip(('requests', 'posted', 'completed', 'attempted', 'cancelled', 'fault'), struct.unpack('<6I', u.mem_read(TOTALS, 24))))
        for reg, value in saved.items():
            assert u.reg_read(reg) == value, 'ABI register changed'
        assert u.reg_read(UC_ARM_REG_SP) == STACK, 'stack changed'
        assert not self.lib.sb_cosim_driven(), 'outputs remain driven at stop'
        log = [dict((k, getattr(self.lib.sb_cosim_log()[i], k)) for k in ('clock', 'kind', 'owner', 'value'))
               for i in range(self.lib.sb_cosim_events())]
        selected_enables = [e for e in log if e['kind'] == 4 and any(start <= e['clock'] < stop and ctrl & RESET and ctrl & (15 << 16) == 10 << 16 for start, stop, addr, ctrl in segments)]
        if not corrupt:
            assert totals['fault'] == 0
            assert totals['requests'] == totals['completed'] == len([e for e in events if e['event'] == 'request'])
            assert totals['requests'] == len([e for e in events if e['event'] == 'TX'])
            assert totals['attempted'] + totals['cancelled'] == totals['completed']
            expected_posts = sum(e['value'] & 0x2000000f == 0x2000000a for e in events if e['event'] == 'request')
            assert totals['posted'] == expected_posts, ('valid snapshot/post count', totals, expected_posts)
        else:
            assert totals['fault'] == 1 and totals['completed'] == 0
        return dict(totals=totals, profile=profile, selected_enable_count=len(selected_enables), events=events, pio_events=log, trace=trace)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('elf', type=Path)
    p.add_argument('library', type=Path)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    runner = Runner(args.elf, args.library)
    cases, examples = 0, {}
    for profile in (1, 2, 3):
        for gap in (6, 7, 8):
            for first, second in ((8, 10), (0x444, 0x446), (0x100, 0x102), (8, 8), (0x10000, 0x10002)):
                result = runner.run([(80, 148, first, READ), (148 + gap, 216 + gap, second, READ)], profile=profile)
                assert result['totals']['requests'] == 2
                assert result['totals']['attempted'] == 2 and result['totals']['cancelled'] == 0
                if profile <= 2:
                    assert result['selected_enable_count'] == 2, 'reply did not drive during physical selection'
                if (first, second) == (8, 10):
                    examples[f'pair-profile{profile}-gap{gap}'] = result
                cases += 1
    for phase in range(0, 90, 3):
        for gap in (6, 7, 8):
            for end_control in (IDLE, READ | (1 << 18), READ & ~RESET):
                segments = [(80, 80 + phase, 8, READ), (80 + phase, 80 + phase + gap, 8, end_control),
                            (80 + phase + gap, 300 + phase + gap, 0x444, READ)]
                runner.run(segments, profile=2)
                cases += 1
    for stall_at in range(80, 240, 4):
        runner.run([(80, 148, 8, READ), (154, 222, 10, READ), (700, 900, 0x444, READ)], profile=2, stall=(stall_at, 500))
        cases += 1
    examples['held-reset'] = runner.run([(80, 148, 8, READ), (148, 800, 8, READ & ~RESET), (850, 1050, 10, READ)], profile=2)
    examples['invalid-snapshot'] = runner.run([(80, 148, 8, READ)], invalid=True)
    examples['bad-token'] = runner.run([(80, 148, 8, READ)], corrupt=True)
    cases += 3
    sequence = [(80 + i * 76, 148 + i * 76, [8, 10, 8, 0x100, 0x102, 0x444, 0x446][i % 7], READ) for i in range(100)]
    examples['continuous100'] = runner.run(sequence, profile=2)
    assert examples['continuous100']['totals']['attempted'] == 100
    assert examples['continuous100']['selected_enable_count'] == 100
    cases += 1
    args.output.write_text(json.dumps(dict(result='PASS', cases=cases,
        scope='actual linked ARM instructions + unchanged PIO; synthetic timing profiles, two-stage input sampling; not cycle-accurate M33',
        profiles={'1': 'one slot/instruction plus multiword saves and skipped IT slots',
                  '2': 'profile1 plus one slot per memory/branch instruction',
                  '3': 'profile1 plus two slots per memory/branch instruction'},
        examples=examples), indent=2) + '\n')
    print(f'PASS: {cases} actual-ARM/PIO scenarios; ownership, counters, cancellation, stalls, reset and ABI')


if __name__ == '__main__':
    main()
