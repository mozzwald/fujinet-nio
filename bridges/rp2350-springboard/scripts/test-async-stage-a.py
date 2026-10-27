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


TABLE=0x20060000

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

    def run(self, schedule, *, profile=1, trace_file=None, stalls=(), injections=(), initial_completed=0, core0_period=128, core0_delay=0, pio_skew=()):
        self.lib.sb_async_co_init()
        for block,clocks in pio_skew:self.lib.sb_async_co_skew(block,clocks)
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
        u.mem_write(ENGINE+24,struct.pack('<I',TABLE))
        u.mem_write(TABLE,b''.join(struct.pack('<H',self.lib.sb_async_co_table(i)) for i in range(32768)))
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
                if core0_period and t >= core0_delay and t % core0_period == 0:
                    self.lib.sb_async_co_retire()
                    for offset in (6,24,28):
                        u.mem_write(TABLE+offset,struct.pack('<H',self.lib.sb_async_co_table(offset//2)))
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
            cost = 1 + (profile - 1 if mnemonic.startswith(('ldr', 'str', 'beq', 'bne', 'bhi', 'cbz', 'cbnz', 'bcc', 'bcs', 'tbb', 'tbh')) else 0)
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
                        events.append(dict(clock=self.lib.sb_cs1_co_clock(), kind=kind, owner=owner,snapshot=tagged & 0xffffffff))
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
            if WORDS <= address < WORDS+65536 or TABLE <= address < TABLE+65536:
                samples = [e for e in events if e['kind']=='SIO' and e['owner']==owner]
                assert samples and samples[-1]['live'], 'invalid address used for lookup'
                if TABLE <= address < TABLE+65536:
                    assert address==TABLE+samples[-1]['low'], 'wrong captured CS1 index'
                    self.lib.sb_async_co_latch(int.from_bytes(uc.mem_read(address,2),'little'))
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
            self.lib.sb_async_co_latch(0xffff)
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
                      accepted=self.lib.sb_async_co_accepted(),
                      capture_fault=self.lib.sb_async_co_fault(),
                      published_status=self.lib.sb_async_co_table(3),
                      published_error=self.lib.sb_async_co_table(12),
                      phase=struct.unpack('<I', u.mem_read(ENGINE + 64, 4))[0], trace=trace)
        for event in events:
            if event['kind']=='TX':
                samples=[e for e in events if e['kind']=='SIO' and e['owner']==event['owner']]
                if samples and not samples[-1]['live']:
                    assert event['cancel_forced'] and event['word']==0, 'invalid source used for reply'
        assert not self.lib.sb_cs1_co_driven()
        return result


def main():
    p=argparse.ArgumentParser();p.add_argument('elf',type=Path);p.add_argument('library',type=Path)
    p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    runner=Runner(args.elf,args.library,'shadow');results={};failures=[]
    cases={}
    for phase in range(8):
      for gap in (5,6,7):
        t=100+phase
        cases[f'cs1-pair-{phase}-{gap}']=( [(t,t+64,0,READ1,0),(t+64+gap,t+128+gap,2,READ1,0)],{},2,0)
        cases[f'write-status-{phase}-{gap}']=( [(t,t+64,26,WRITE|(1<<20),0),(t+64+gap,t+128+gap,6,READ1,0)],{},1,1)
        cases[f'write-rom-{phase}-{gap}']=( [(t,t+64,26,WRITE|(1<<20),0),(t+64+gap,t+128+gap,8,READ0,0)],{},1,1)
        seq=[]
        for i in range(8):
          st=t+i*(64+gap);seq.append((st,st+64,8 if i%2==0 else 0,READ0 if i%2==0 else READ1,0))
        cases[f'alternation-{phase}-{gap}']=(seq,{},8,0)
    cases['rom-pair']=([(100,164,8,READ0,0),(170,234,10,READ0,0)],{},2,0)
    cases['accept-poll']=([(100,164,26,WRITE|(1<<20),0),(170,234,28,READ1,0),(420,484,28,READ1,0)],{},2,1)
    cases['cancel-write']=([(100,164,26,WRITE|(1<<20),0),(164,180,26,RESET|(7<<16),0),(420,484,28,READ1,0)],{},1,0)
    cases['duplicate']=([(100,164,26,WRITE|(1<<20),0),(420,484,26,WRITE|(1<<20),0),(750,814,28,READ1,0)],{},1,1)
    cases['two-writes']=([(100,164,26,WRITE|(1<<20),0),(170,234,26,WRITE|(2<<20),0),(600,664,28,READ1,0)],{},1,2)
    cases['late-response']=([(100,164,0,READ1,0)],{'stalls':[(110,220)]},0,0)
    cases['release-stall']=([(100,164,0,READ1,0),(420,484,8,READ0,0)],{'stalls':[(160,300)]},2,0)
    cases['held-reset']=([(100,164,8,0xa0000,0),(300,364,0,READ1,0)],{},0,0)
    for kind in range(4):
      cases[f'fault-{kind}']=([(100,164,26,WRITE|(1<<20),0),(420,484,6,READ1,0)],{'injections':[(130,kind)]},0,0)
    cases['overflow']=([(100+i*70,164+i*70,26,WRITE|((i+1)<<20),0) for i in range(6)],{'core0_delay':650},0,0)
    for phase in range(8):
      t=100+phase
      cases[f'write-cancel-recover-{phase}']=( [(t,t+64,26,WRITE|(1<<20),0),(t+64,t+80,26,RESET|(7<<16),0),(420,484,26,WRITE|(1<<20),0),(750,814,28,READ1,0)],{},1,1)
      cases[f'stale-address-{phase}']=( [(t,t+64,0,READ1,0),(t+70,t+134,8,READ0,0),(500,564,0,READ1,0)],{'stalls':[(t+10,t+160)]},None,0)
      cases[f'late-after-sample-{phase}']=( [(t,t+64,0,READ1,0),(500,564,0,READ1,0)],{'stalls':[(t+30,t+170)]},None,0)
    cases['upper-read']=([(100,164,0x10000,READ1,0),(300,364,0x10008,READ0,0)],{},2,0)
    cases['reserved-read']=([(100,164,62,READ1,0)],{},1,0)
    cases['unsupported-write']=([(100,164,2,WRITE|(1<<20),0),(420,484,6,READ1,0)],{},1,0)
    cases['sequence-write']=([(100,164,26,WRITE|(3<<20),0),(420,484,6,READ1,0)],{},1,0)
    for low_skew in range(5):
      for high_skew in range(5):
        skew={'pio_skew':[(0,low_skew),(2,high_skew)]}
        for phase in range(4):
          t=100+phase
          cases[f'block-phase-{low_skew}-{high_skew}-{phase}']=( [(t,t+64,26,WRITE|(1<<20),0),(t+70,t+134,6,READ1,0),(420,484,28,READ1,0)],skew,2,1)
          cases[f'block-cancel-{low_skew}-{high_skew}-{phase}']=( [(t,t+64,26,WRITE|(1<<20),0),(t+64,t+70,26,RESET|(7<<16),0),(420,484,28,READ1,0)],skew,1,0)
    # CS1 ends before WE, including before capture handshake has finished.
    # No such cancelled transaction may publish PROBE_ACK.
    for width in (2,4,8,12,16,24,32):
      for phase in range(5):
        for skew in ((),((0,2),),((2,2),)):
          t=100+phase;label='none' if not skew else str(skew[0][0])
          cases[f'partial-cancel-{width}-{phase}-{label}']=( [(t,t+width,26,WRITE|(1<<20),0),(t+width,t+width+12,26,RESET|(7<<16),0),(420,484,28,READ1,0)],{'pio_skew':skew},1,0)
          cases[f'partial-recover-{width}-{phase}-{label}']=( [(t,t+width,26,WRITE|(1<<20),0),(t+width,t+width+12,26,RESET|(7<<16),0),(420,484,26,WRITE|(1<<20),0),(750,814,28,READ1,0)],{'pio_skew':skew},1,1)
    for width in (2,4,8,12,16):
      for phase in range(8):
        t=100+phase
        for gap in (5,6,7):
          # First cancelled write targets a read-only address with data1.
          # Next valid write targets PROBE with token2 (must be rejected).
          # Combining first data and second address would falsely ACK1.
          cases[f'partial-reselect-{width}-{phase}-{gap}']=( [(t,t+width,2,WRITE|(1<<20),0),(t+width,t+width+2,2,RESET|(7<<16),0),(t+width+2+gap,t+width+66+gap,26,WRITE|(2<<20),0),(750,814,28,READ1,0)],{},1,0)
    for phase in range(8):
      for delta in range(-4,5):
        t=100+phase;end=t+64;schedule=[(t,end,26,WRITE|(1<<20),0)]
        if delta<0:schedule.append((end,end-delta,26,RESET|(7<<16),0))
        if delta>0:schedule.append((end,end+delta,26,RESET|(13<<16),0))
        schedule.append((420,484,28,READ1,0))
        cases[f'ending-priority-{phase}-{delta}']=(schedule,{},1,int(delta>=0))
    for name,(schedule,options,nreads,nwrites) in cases.items():
      for profile in (1,2):
        key=f'{name}/p{profile}'
        try:
          r=runner.run(schedule,profile=profile,**options)
          trace=args.output.parent/'traces'/f"{name}-p{profile}.json.gz";trace.parent.mkdir(exist_ok=True,parents=True)
          import gzip
          with gzip.open(trace,'wt') as f:json.dump(r['trace'],f)
          for lookup_event in [e for e in r['events'] if e['kind']=='lookup']:
            expected_fixed={TABLE:0x464e,TABLE+2:0x0100,TABLE+4:1,TABLE+8:1}.get(lookup_event['address'])
            if expected_fixed is not None:
              tx=next(e for e in r['events'] if e['kind']=='TX' and e['owner']==lookup_event['owner'])
              assert tx['word']==expected_fixed,('fixed ABI value',tx,expected_fixed)
          fault_case=name.startswith('fault-') or name in ('held-reset','overflow')
          if fault_case:
            assert not r['accepted'],r['accepted']
            if name!='held-reset':assert r['published_status']&16
            if name.startswith('fault-'):
              assert r['enables']==1 and all(w['driven_before_end'] for w in r['read_windows'])
            if name=='held-reset':
              assert r['enables']==1 and all(w['driven_before_end'] for w in r['read_windows'])
            if name=='overflow':assert r['published_error']==7
          else:
            if not (name.startswith('partial-recover-') and r['published_status']&16):assert r['accepted']==nwrites,(r['accepted'],nwrites)
            if name.startswith('partial-recover-') and r['published_status']&16:
              assert r['accepted']==0 and r['published_error']==7
              assert all(w['driven_before_end'] for w in r['read_windows'])
            elif nreads is None:
              assert r['totals']['cancelled']>=1 and r['enables']>=1
              assert r['totals']['requests']==r['totals']['posted']==r['totals']['completed']
            elif name!='late-response':
              assert r['enables']==nreads,(r['enables'],nreads)
              assert all(w['driven_before_end'] for w in r['read_windows']),r['read_windows']
              assert r['totals']['requests']==r['totals']['completed']==r['totals']['posted']==nreads,r['totals']
              assert r['totals']['cancelled']==0,r['totals']
            else:assert r['totals']['cancelled']==1 and not r['enables']
          intervals=[];prev=None
          for ins in r['trace']:
            if 'ldr' in ins['instruction'] and '[r3, #4]' in ins['instruction']:
              if prev:intervals.append(ins['clock']-prev)
              prev=ins['clock']
          r['max_read_poll_interval']=max(intervals,default=0);r['pass']=True
          trace=args.output.parent/'traces'/f"{name}-p{profile}.json.gz";trace.parent.mkdir(exist_ok=True,parents=True)
          import gzip
          with gzip.open(trace,'wt') as f:json.dump(r.pop('trace'),f)
          results[key]=r
        except Exception as e:
          failures.append(key);results[key]={**{k:v for k,v in r.items() if k!='trace'},'pass':False,'error':str(e)}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps({'backend':'mapped-register','profiles':'synthetic, not M33 hardware bounds','results':results,'failures':failures},indent=2)+'\n')
    print('PASS',len(results)-len(failures),'/',len(results),'FAIL',failures)
    raise SystemExit(bool(failures))
if __name__=='__main__':main()
