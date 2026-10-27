#!/usr/bin/env python3
"""Execute compiled Phase1 ARM against the same strict native PIO harness.

Instruction-cost profiles are assumptions, not measured Cortex-M33 cycles.
GNU push/pop aliases retain the same eight-clock surcharge. The shadow-MMIO
backend avoids the ITSTATE corruption reproduced by test-unicorn-mmio.py.
No UART, USB, programmer or Palm device is opened by this test.
"""
import argparse
import ctypes as C
import json
from pathlib import Path
import re
import struct
import subprocess
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import *

RESET = 1 << 45
IDLE, READ0, READ1, WRITE = (RESET | (x << 16) for x in (15, 10, 9, 5))
ENGINE, BUS, STOP, STACK, RETURN, WORDS = (0x200f0000, 0x200f0100, 0x200f1000,
                                         0x200fd000, 0x200fe000, 0x20040000)


def word(address):
    return {8: 0xfeed, 10: 0xbeef, 0x444: 0xa174, 0x446: 0x5c8f}.get(
        address, ((address * 40503) ^ 0xa55a) & 65535)


class Runner:
    def __init__(self, elf, library, mmio_backend="shadow"):
        self.mmio_backend = mmio_backend
        self.lib = C.CDLL(str(library.resolve()))
        self.lib.sb_cs1_co_bus.argtypes = [C.c_uint64, C.c_uint, C.c_uint]
        self.lib.sb_cs1_co_rx.restype = C.c_uint64
        with elf.open('rb') as stream:
            e = ELFFile(stream)
            self.segments = [(s['p_vaddr'], s.data()) for s in e.iter_segments()
                             if s['p_type'] == 'PT_LOAD' and 0x20000000 <= s['p_vaddr'] < 0x20100000]
            self.symbols = {s.name: s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
        dis = subprocess.check_output(['arm-none-eabi-objdump', '-d', str(elf)], text=True)
        self.lines = {int(m[1], 16): m[2] for m in re.finditer(
            r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{4}\s+)+(.+)$', dis, re.M)}

    def run(self, schedule, *, profile=1, trace_file=None, stalls=(), injections=(), initial_completed=0):
        self.lib.sb_cs1_co_init()
        u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
        u.mem_map(0x20000000, 0x100000)
        for address, data in self.segments:
            u.mem_write(address, data)
        u.mem_write(WORDS, b''.join(struct.pack('<H', word(a)) for a in range(0, 65536, 2)))
        u.mem_write(ENGINE, bytes(96))
        u.mem_write(ENGINE, struct.pack('<4I', initial_completed, initial_completed,
                                        initial_completed, initial_completed))
        u.mem_write(ENGINE + 24, struct.pack('<II', 0x50200000, 0x50400000))
        u.mem_write(ENGINE + 32, struct.pack('<4H',0x4653,1,0,0xffff))
        u.mem_write(BUS, struct.pack('<III', 0x50300000, 0xd0000004, STOP))
        u.mem_write(STOP, b'\0')
        for reg, val in [(UC_ARM_REG_R0, ENGINE), (UC_ARM_REG_R1, WORDS),
                         (UC_ARM_REG_R2, BUS), (UC_ARM_REG_SP, STACK),
                         (UC_ARM_REG_LR, RETURN | 1)]:
            u.reg_write(reg, val)
        saved = {reg: 0x12340000 + i for i, reg in enumerate(range(UC_ARM_REG_R4, UC_ARM_REG_R12))}
        for reg, val in saved.items():
            u.reg_write(reg, val)
        end = max(s[1] for s in schedule) + 500
        previous, returned, owner, expected_phase, sent = None, False, 0, 1, False
        trace, events = [], []
        previous_drive = False
        read_windows = []
        injected = set()
        cancel_forced = False
        log = trace_file.open('w', buffering=1) if trace_file else None

        def advance(clocks):
            nonlocal previous_drive
            remaining = clocks
            while remaining:

                t = self.lib.sb_cs1_co_clock()
                if not any(start <= t < stop for start,stop in stalls):
                    remaining -= 1
                for i,(when,kind) in enumerate(injections):
                    if t>=when and i not in injected:
                        self.lib.sb_cs1_co_inject(kind)
                        injected.add(i)
                controls, address, scratch = IDLE, 0, 0
                for start, stop, addr, pins, value in schedule:
                    if start <= t < stop:
                        controls, address, scratch = pins, addr, value
                        break
                else:
                    for start, stop, addr, pins, value in schedule:
                        if stop <= t:
                            scratch = value
                self.lib.sb_cs1_co_bus(controls, address, scratch)
                self.lib.sb_cs1_co_step(1)
                drive = bool(self.lib.sb_cs1_co_driven())
                if drive != previous_drive:
                    events.append(dict(clock=self.lib.sb_cs1_co_clock(),
                                       kind='enable' if drive else 'release'))
                previous_drive = drive
                for start, stop, addr, pins, value in schedule:
                    if t==stop-1 and pins in (READ0,READ1):
                        read_windows.append(dict(start=start,stop=stop,address=addr,
                                                 driven_before_end=drive))
            if self.lib.sb_cs1_co_clock() >= end:
                u.mem_write(STOP, b'\1')

        def code(uc, address, size, _):
            nonlocal previous, returned
            if address == RETURN:
                returned = True
                uc.emu_stop()
                return
            op = self.lines[address]
            mnemonic = op.split()[0]
            cost = 1 + (profile - 1 if mnemonic.startswith(('ldr', 'str', 'beq', 'bne', 'bhi', 'cbz', 'cbnz', 'bcc', 'bcs')) else 0)
            if mnemonic.startswith(('push', 'pop')) or ('sp!' in op and mnemonic.startswith(('stmdb','ldmia'))):
                cost += 8
            if previous and not previous[2].startswith(('b', 'cbz', 'cbnz', 'pop', 'ldm', 'tbh', 'tbb')):
                lo = previous[0] + previous[1]
                if lo < address <= lo + 12:
                    cost += sum(lo <= p < address for p in self.lines)
            record = dict(clock=self.lib.sb_cs1_co_clock(), pc=f'{address:08x}', instruction=op)
            trace.append(record)
            if log:
                log.write(json.dumps(record) + '\n')
            advance(cost)
            previous = address, size, mnemonic

        def mmio_read(block):
            def read(uc, offset, size, _):
                nonlocal owner, expected_phase, sent, cancel_forced
                if 0x20 <= offset < 0x30:
                    sm = (offset - 0x20) // 4
                    tagged = self.lib.sb_cs1_co_rx(block, sm)
                    if block == 1 and sm == 0:
                        kind = (tagged >> 32) & 3
                        new_owner = tagged >> 34
                        assert kind == expected_phase, ('RX phase', kind, expected_phase)
                        if kind == 2:
                            assert owner == new_owner and sent
                            expected_phase = 1
                        else:
                            expected_phase, sent, cancel_forced = 2, False, False
                        owner = new_owner
                        events.append(dict(clock=self.lib.sb_cs1_co_clock(), kind=kind, owner=owner))
                    else:
                        events.append(dict(clock=self.lib.sb_cs1_co_clock(),kind='write_RX',
                                           block=block,sm=sm,word=tagged & 0xffffffff))
                    return tagged & 0xffffffff
                return self.lib.sb_cs1_co_reg(block, offset)
            return read

        def mmio_write(block):
            def write(uc, offset, size, value, _):
                nonlocal sent, cancel_forced
                assert block == 1
                if offset == 0x10:
                    assert expected_phase == 2 and not sent, 'unowned/duplicate TX'
                    sent = True
                    self.lib.sb_cs1_co_tx(value, owner)
                    events.append(dict(clock=self.lib.sb_cs1_co_clock(), kind='TX', owner=owner, word=value, cancel_forced=cancel_forced))
                elif offset == 0x34:
                    if value & 1: cancel_forced = True
                    self.lib.sb_cs1_co_irq_force(value)
                elif offset == 0x30:
                    self.lib.sb_cs1_co_irq_clear(value)
                else:
                    raise AssertionError(('unexpected PIO write', block, offset))
            return write

        def lookup(uc, access, address, size, value, _):
            if WORDS <= address < WORDS+65536 or ENGINE+32 <= address < ENGINE+40:
                samples = [e for e in events if e['kind']=='SIO' and e['owner']==owner]
                assert samples and samples[-1]['live'], 'invalid address used for lookup'
                events.append(dict(clock=self.lib.sb_cs1_co_clock(),kind='lookup',owner=owner,address=address))
        u.hook_add(UC_HOOK_CODE, code)
        u.hook_add(UC_HOOK_MEM_READ, lookup)
        def map_registers(base, read, write=None):
            if self.mmio_backend == 'callback':
                u.mmio_map(base,0x1000,read,None,write,None)
                return
            # Normal mapped pages avoid Unicorn's MMIO callback ITSTATE
            # restoration. Hooks still perform every access at its original
            # instruction boundary; the strict native FIFO model is unchanged.
            u.mem_map(base,0x1000)
            def before_read(uc,access,address,size,value,_):
                result=read(uc,address-base,size,None)
                uc.mem_write(address,int(result).to_bytes(size,'little'))
            def before_write(uc,access,address,size,value,_):
                assert write is not None, 'write to read-only registers'
                write(uc,address-base,size,value,None)
            u.hook_add(UC_HOOK_MEM_READ,before_read,begin=base,end=base+0xfff)
            u.hook_add(UC_HOOK_MEM_WRITE,before_write,begin=base,end=base+0xfff)
        for block in range(3):
            map_registers(0x50200000 + block * 0x100000,
                          mmio_read(block),mmio_write(block))
        def sio_read(uc, offset, size, _):
            assert offset==4
            value = self.lib.sb_cs1_co_gpio()
            events.append(dict(clock=self.lib.sb_cs1_co_clock(),kind='SIO' if uc.reg_read(UC_ARM_REG_PC)==(self.symbols['sb_cs1_sample_address'] & ~1) else 'control_probe',low=value & 0xfffe,
                               owner=owner,live=bool(self.lib.sb_cs1_co_selected()) and
                               self.lib.sb_cs1_co_owner()==owner))
            return value
        map_registers(0xd0000000,sio_read)
        try:
            u.emu_start(self.symbols['sb_cs1_reply'], RETURN + 2, count=200000)
        finally:
            if log:
                log.close()
        assert returned, 'ARM engine failed to stop'
        for reg, value in saved.items():
            assert u.reg_read(reg) == value, 'callee-saved register changed'
        assert u.reg_read(UC_ARM_REG_SP) == STACK
        totals = dict(zip(('requests', 'posted', 'completed', 'attempted', 'cancelled', 'fault'),
                          struct.unpack('<6I', u.mem_read(ENGINE, 24))))
        fields = struct.unpack('<HH6I', u.mem_read(ENGINE + 36, 28))
        regs = dict(zip(('scratch','reserved','fault','captured','committed',
                         'ignored','rejected','next_epoch'),fields))
        result = dict(profile=profile, totals=totals, register_state=regs,
                      enables=self.lib.sb_cs1_co_enables(), events=events,
                      read_windows=read_windows,
                      phase=struct.unpack('<I', u.mem_read(ENGINE + 64, 4))[0], trace=trace)
        for event in events:
            if event['kind']=='TX':
                samples=[e for e in events if e['kind']=='SIO' and e['owner']==event['owner']]
                if samples and not samples[-1]['live']:
                    assert event['cancel_forced'] and event['word']==0, 'invalid source used for reply'
        assert not self.lib.sb_cs1_co_driven()
        return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('elf', type=Path)
    p.add_argument('library', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--trace', type=Path)
    p.add_argument('--case', help='Run one named schedule')
    p.add_argument('--profile', type=int, choices=(1,2))
    p.add_argument('--mmio-backend',choices=('shadow','callback'),default='shadow')
    args = p.parse_args()
    runner = Runner(args.elf, args.library,args.mmio_backend)
    schedules = {
        'rom_pair': [(100,164,8,READ0,0),(170,234,10,READ0,0)],
        'cs1_id_abi': [(100,164,0,READ1,0),(170,234,2,READ1,0)],
        'write_read': [(100,164,4,WRITE | (0x1234<<20),0),
                       (170,234,4,READ1,0x1234)],
        'paced_write_read': [(100,228,4,WRITE | (0x1234<<20),0),
                             (420,548,4,READ1,0x1234)],
        'paced_write_rom_read': [(100,228,4,WRITE | (0x1234<<20),0),
                                 (420,484,8,READ0,0x1234),
                                 (490,554,10,READ0,0x1234),
                                 (750,878,4,READ1,0x1234)],
        'cancelled_write_read': [(100,164,4,WRITE | (0x1234<<20),0),
                                 (164,180,4,RESET | (7<<16) | (0x1234<<20),0),
                                 (420,548,4,READ1,0)],
        'ignored_write_read': [(100,228,0,WRITE | (0x1234<<20),0),
                               (420,548,4,READ1,0)],
        'upper_write_read': [(100,228,0x10004,WRITE | (0x1234<<20),0),
                             (420,548,4,READ1,0)],
        'write_rom_pair_read': [(100,164,4,WRITE | (0x1234<<20),0),
                               (170,234,8,READ0,0x1234),
                               (240,304,10,READ0,0x1234),
                               (310,374,4,READ1,0x1234)],
    }
    results = {}
    failures = []
    if args.case and args.case not in schedules:
        p.error('Unknown schedule: '+args.case)
    for name, schedule in schedules.items():
        if args.case and args.case != name:
            continue
        for profile in ((args.profile,) if args.profile else (1,2)):
            trace_file = args.trace.with_name(f'{args.trace.stem}-{name}-p{profile}{args.trace.suffix}') if args.trace else None
            result = runner.run(schedule,profile=profile,trace_file=trace_file)
            results[f'{name}/p{profile}'] = result
            expected = sum(ctrl in (READ0,READ1) for _,_,_,ctrl,_ in schedule)
            totals = result['totals']
            passed = (totals['fault']==0 and result['enables']==expected and
                      all(totals[k]==expected for k in ('requests','posted','completed','attempted')) and
                      totals['cancelled']==0 and result['phase']==0 and
                      result['register_state']['fault']==0 and
                      len(result['read_windows'])==expected and
                      all(w['driven_before_end'] for w in result['read_windows']))
            if 'write' in name:
                regs = result['register_state']
                cancelled = name=='cancelled_write_read'
                ignored = name in ('ignored_write_read','upper_write_read')
                passed = (passed and regs['committed']==int(not (cancelled or ignored)) and
                          regs['scratch']==(0 if cancelled or ignored else 0x1234) and
                          regs['captured']==1 and regs['next_epoch']==0xffffffff and
                          regs['ignored']==int(ignored) and regs['rejected']==int(cancelled))
            result['passed'] = passed
            args.output.write_text(json.dumps(results,indent=2)+'\n')
            if not passed:
                failures.append((name,profile,result['enables'],expected,result['totals']))
            print(name,profile,'PASS' if passed else 'FAIL',result['totals'],result['register_state'],flush=True)
    assert not failures, failures


if __name__ == '__main__':
    main()
