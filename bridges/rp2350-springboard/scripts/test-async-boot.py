#!/usr/bin/env python3
"""Execute compiled one-shot boot admission on M33; no hardware/MMIO."""
import argparse,json,struct,subprocess,tempfile
from pathlib import Path
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import *
def run(binary,entry):
 rows=[]
 for bits in range(128):
  u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33);u.mem_map(0x20000000,0x10000);u.mem_write(0x20001000,binary)
  used=bits&1;u.mem_write(0x20002000,bytes([used]));u.mem_write(0x2000f000,struct.pack('<3I',bool(bits&16),bool(bits&32),bool(bits&64)))
  for reg,value in ((UC_ARM_REG_R0,0x20002000),(UC_ARM_REG_R1,bool(bits&2)),(UC_ARM_REG_R2,bool(bits&4)),(UC_ARM_REG_R3,bool(bits&8)),(UC_ARM_REG_SP,0x2000f000),(UC_ARM_REG_LR,0x2000ff01)):u.reg_write(reg,int(value))
  u.emu_start(entry|1,0x2000ff00,count=1000)
  expected=not used and bool(bits&2) and bool(bits&4) and bool(bits&8) and not bits&16 and not bits&32 and not bits&64
  assert u.reg_read(UC_ARM_REG_R0)==int(expected)
  assert u.mem_read(0x20002000,1)==bytes([bool(used or expected)])
  rows.append(dict(inputs=bits,requested=bool(expected),pass_=True))
 return rows
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('binary',type=Path);p.add_argument('--entry',type=lambda x:int(x,0),default=0x20001000);p.add_argument('--output',type=Path,required=True);a=p.parse_args();rows=run(a.binary.read_bytes(),a.entry);a.output.write_text(json.dumps(dict(compiled_boot_cases=rows,passed=128),indent=2)+'\n');print('PASS128 compiled M33 one-shot boot states')
