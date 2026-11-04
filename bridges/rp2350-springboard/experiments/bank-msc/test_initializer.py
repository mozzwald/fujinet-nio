#!/usr/bin/env python3
from pathlib import Path
import argparse,json,struct,subprocess,tempfile
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import *
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description="Compare real linked PIO initializer instruction RAM with native APIO model. No device access.")
p.add_argument('elf',type=Path);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();elf=a.elf;out=a.output
if out.exists():p.error('output must be new to preserve evidence')
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'dump.c').write_text('#include <stdio.h>\n#include <stdbool.h>\n#include <apio.h>\n_apio_emulated_pio_t _apio_emulated_pio;\n_apio_emulated_gpio_t _apio_emulated_gpios;\nvoid sb_pio_rearm_program_init(bool);\nint main(void){sb_pio_rearm_program_init(false);for(unsigned b=0;b<3;b++)for(unsigned i=0;i<32;i++){unsigned short x=_apio_emulated_pio.instr[b][i];fwrite(&x,2,1,stdout);}return 0;}\n')
 subprocess.run(['cc','-std=c11','-DAPIO_EMULATION=1','-I'+str(root/'.deps/apio/include'),'-I'+str(root/'src'),'-I'+str(root/'experiments/bank-msc'),str(p/'dump.c'),str(root/'experiments/bank-msc/program.c'),'-o',str(p/'dump')],check=True)
 expected=struct.unpack('<96H',subprocess.check_output([str(p/'dump')]))
 config_source=(p/'dump.c').read_text();start=config_source.index('for(unsigned b=0;b<3;b++)');end=config_source.index('return 0;',start)
 config_source=config_source[:start]+"for(unsigned b=0;b<3;b++){fwrite(&_apio_emulated_pio.gpio_base[b],4,1,stdout);for(unsigned sm=0;sm<4;sm++){pio_sm_reg_t *r=&_apio_emulated_pio.pio_sm_reg[b][sm];fwrite(&r->clkdiv,4,1,stdout);fwrite(&r->execctrl,4,1,stdout);fwrite(&r->shiftctrl,4,1,stdout);fwrite(&r->pinctrl,4,1,stdout);}}"+config_source[end:]
 (p/'config.c').write_text(config_source)
 subprocess.run(['cc','-std=c11','-DAPIO_EMULATION=1','-I'+str(root/'.deps/apio/include'),'-I'+str(root/'src'),'-I'+str(root/'experiments/bank-msc'),str(p/'config.c'),str(root/'experiments/bank-msc/program.c'),'-o',str(p/'config')],check=True)
 configurations=struct.unpack('<51I',subprocess.check_output([str(p/'config')]))
with elf.open('rb') as f:
 e=ELFFile(f);s={x.name:x['st_value'] for x in e.get_section_by_name('.symtab').iter_symbols()};seg=[(x['p_vaddr'],x.data()) for x in e.iter_segments() if x['p_type']=='PT_LOAD']
u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
for at,n in ((0x10000000,0x1000000),(0x20000000,0x100000),(0x40000000,0x2000000),(0x50000000,0x500000),(0xd0000000,0x1000)):u.mem_map(at,n)
for at,data in seg:
 if data:u.mem_write(at,data)
u.mem_write(0x40020008,struct.pack('<I',0xffffffff));ret=0x2007f100
u.reg_write(UC_ARM_REG_SP,0x2007e000);u.reg_write(UC_ARM_REG_LR,ret|1);u.reg_write(UC_ARM_REG_R0,0)
u.emu_start(s['sb_pio_rearm_program_init']|1,ret,count=2000000)
assert u.reg_read(UC_ARM_REG_PC)==ret,'initializer did not return'
rows=[];diff=[]
for b,n in enumerate((32,29,30)):
 for i in range(n):
  a=int.from_bytes(u.mem_read(0x50200048+b*0x100000+4*i,4),'little');ex=expected[b*32+i]
  rows.append(dict(block=b,pc=i,expected=f'{ex:04x}',actual=f'{a:04x}'))
  if a!=ex:diff.append(rows[-1])
config_rows=[];vals=iter(configurations)
for b in range(3):
 base=0x50200000+b*0x100000
 ex=next(vals);got=int.from_bytes(u.mem_read(base+0x168,4),'little');assert ex==got
 config_rows.append(dict(block=b,field='GPIOBASE',value=got))
 for sm in range(4):
  for field,offset in (('clkdiv',0),('execctrl',4),('shiftctrl',8),('pinctrl',20)):
   ex=next(vals);got=int.from_bytes(u.mem_read(base+0xc8+0x18*sm+offset,4),'little');assert ex==got,(b,sm,field,ex,got)
   config_rows.append(dict(block=b,sm=sm,field=field,value=got))
r=dict(configuration_fields=config_rows,elf=str(elf),result='FAIL' if diff else 'PASS',mismatches=diff,instructions=rows,scope='Actual linked non-APIO_EMULATION ARM initializer vs native APIO_EMULATION instruction RAM; reset-done and MMIO mapped, no hardware access')
out.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(dict(result=r['result'],mismatches=diff),indent=2))

raise SystemExit(bool(diff))
