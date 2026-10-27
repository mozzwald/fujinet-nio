#!/usr/bin/env python3
"""Regression for predicated SRAM load -> MMIO access -> ordinary Thumb load.

The expected result follows the actual instructions: TST(0) selects LDRH(FEED),
FSTAT(0xF00) does not take BEQ, and LDRB(stop=0) selects GOOD. A callback-backend
failure is recorded, never used as a firmware timing result. The shadow backend
must execute LDRB and return GOOD with the same read/write callback values.
"""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import unicorn
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import *

SOURCE = '''.syntax unified
.thumb
.global entry
.thumb_func
entry:
 tst r0,r0
 ite ne
 movwne r2,#0xffff
 ldrheq.w r2,[r1,r0,lsl #1]
 str.w r2,[r3,#16]
 b wait
wait:
 ldr r4,[r3,#4]
 tst r4,#0x100
 beq bad
 ldrb.w r4,[r5]
 cmp r4,#0
 beq good
bad:
 movs r6,#1
 bx lr
good:
 movs r6,#0
 bx lr
'''


def run(binary, backend, observe_reads=True):
    base,regs,stop,ret = 0x20000000,0x50300000,0x20001000,0x20002000
    u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
    u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
    u.mem_map(base,0x10000)
    u.mem_write(base,binary)
    u.mem_write(base+0x100,b'\xed\xfe')
    u.mem_write(stop,b'\0')
    for reg,value in ((UC_ARM_REG_R0,0),(UC_ARM_REG_R1,base+0x100),
                      (UC_ARM_REG_R3,regs),(UC_ARM_REG_R5,stop),
                      (UC_ARM_REG_R6,0xdead),(UC_ARM_REG_LR,ret|1)):
        u.reg_write(reg,value)
    writes=[]
    trace=[]
    def read(uc,offset,size,_):
        assert offset==4 and size==4
        return 0xf00
    def write(uc,offset,size,value,_):
        assert offset==16 and size==4
        writes.append(value)
    if backend=='callback':
        u.mmio_map(regs,0x1000,read,None,write,None)
    else:
        u.mem_map(regs,0x1000)
        def shadow_read(uc,access,address,size,value,_):
            uc.mem_write(address,read(uc,address-regs,size,None).to_bytes(size,'little'))
        def shadow_write(uc,access,address,size,value,_):
            write(uc,address-regs,size,value,None)
        u.hook_add(UC_HOOK_MEM_READ,shadow_read,begin=regs,end=regs+0xfff)
        u.hook_add(UC_HOOK_MEM_WRITE,shadow_write,begin=regs,end=regs+0xfff)
    def code(uc,address,size,_):
        trace.append(dict(offset=address-base,cpsr=hex(uc.reg_read(UC_ARM_REG_CPSR)),
                          r4=uc.reg_read(UC_ARM_REG_R4)))
    if observe_reads:
        u.hook_add(UC_HOOK_MEM_READ,lambda *args: None)
    u.hook_add(UC_HOOK_CODE,code)
    u.emu_start(base|1,ret,count=200)
    passed = u.reg_read(UC_ARM_REG_R6)==0 and u.reg_read(UC_ARM_REG_R4)==0 and writes==[0xfeed]
    return dict(passed=passed,result=u.reg_read(UC_ARM_REG_R6),writes=writes,trace=trace)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='cs1-itstate-') as name:
        d=Path(name)
        (d/'repro.S').write_text(SOURCE)
        subprocess.run(['arm-none-eabi-gcc','-mcpu=cortex-m33','-mthumb','-c',str(d/'repro.S'),'-o',str(d/'repro.o')],check=True)
        subprocess.run(['arm-none-eabi-objcopy','-O','binary','-j','.text',str(d/'repro.o'),str(d/'repro.bin')],check=True)
        data=(d/'repro.bin').read_bytes()
        result=dict(unicorn=unicorn.__version__,source=SOURCE,machine_code=data.hex(),
                    disassembly=subprocess.check_output(['arm-none-eabi-objdump','-d',str(d/'repro.o')],text=True),
                    backends={kind:run(data,kind) for kind in ('callback','shadow')},
                    controls={kind:run(data,kind,False) for kind in ('callback','shadow')})
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    assert all(r['passed'] for r in result['controls'].values()), 'unobserved control failed'
    assert result['backends']['shadow']['passed'], 'shadow MMIO failed Thumb instruction semantics'
    print('Shadow MMIO semantics PASS; callback backend:',result['backends']['callback']['passed'])

if __name__=='__main__':main()
