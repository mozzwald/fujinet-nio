"""Experimental instruction-boundary mapped-register backend.

Execute the original compiled ARM unchanged. Observe MMIO at its instruction
boundary after the original cost advance, with ordinary mapped load/store RAM.
Avoid Unicorn READ/WRITE hooks which can restore stale ITSTATE on this sequence.
All existing ownership/lookup/FIFO assertions are retained. Host only.
"""
from pathlib import Path
import types
root=Path(__file__).resolve().parents[2]
text=(root/'scripts/test-async-stage-a.py').read_text()
text=text.replace('from unicorn.arm_const import *','from unicorn.arm_const import *\nfrom unicorn import UC_MEM_READ',1)
def replace(old,new):
 global text
 assert text.count(old)==1,old
 text=text.replace(old,new,1)
replace('            advance(cost)\n            previous = address, size, mnemonic',
'''            advance(cost)
            if self.mmio_backend == 'instruction':
                instruction_access(uc, op)
            previous = address, size, mnemonic''')
replace('        u.hook_add(UC_HOOK_MEM_READ, lookup)\n',
'''        if self.mmio_backend != 'instruction':
            u.hook_add(UC_HOOK_MEM_READ, lookup)
        mappings = {}
''')
replace('        def map_registers(base, read, write=None):\n',
'''        def map_registers(base, read, write=None):
            if self.mmio_backend == 'instruction':
                u.mem_map(base, 0x1000)
                mappings[base] = (read, write)
                return
''')
replace('        try:\n            u.emu_start',
'''        def instruction_access(uc, op):
            # All indirect loads/stores emitted by this worker use this narrow
            # syntax. PC literals and stack push/pop are regular SRAM accesses.
            hit=re.match(r'^(ldr\\w*|str\\w*)(?:\\.w|\\.n)?\\s+(\\w+),\\s*\\[([^]]+)\\](.*)$', op)
            if not hit:
                if op.startswith(('ldr','str')):
                    raise AssertionError(('unsupported load/store',op))
                return
            ins, target, inside, suffix=hit.groups()
            parts=[x.strip() for x in inside.split(',')]
            if parts[0]=='pc':return
            aliases={'sl':10,'fp':11,'ip':12,'sb':9,'sp':13,'lr':14}
            def register(name):
                if name.startswith('r') and name[1:].isdigit():return globals()['UC_ARM_REG_R'+name[1:]]
                assert name in aliases,('unknown address register',name,op)
                number=aliases[name]
                return UC_ARM_REG_SP if number==13 else UC_ARM_REG_LR if number==14 else globals()['UC_ARM_REG_R'+str(number)]
            address=uc.reg_read(register(parts[0]))
            if len(parts)>1:
                if parts[1].startswith('#'):offset=int(parts[1][1:],0)
                else:offset=uc.reg_read(register(parts[1]))
                if len(parts)>2:
                    assert len(parts)==3 and parts[2].startswith('lsl #'),('unsupported index',op)
                    offset <<= int(parts[2][5:],0)
                address=(address+offset)&0xffffffff
            assert not suffix.strip().startswith(('!',',')),('unsupported writeback',op)
            width=1 if ins.startswith(('ldrb','strb')) else 2 if ins.startswith(('ldrh','strh')) else 4
            slot=next(((base,read,write) for base,(read,write) in mappings.items() if base<=address<base+0x1000),None)
            if ins.startswith('ldr'):
                if slot:
                    base,read,write=slot
                    value=read(uc,address-base,width,None)
                    uc.mem_write(address,(int(value)&((1<<(width*8))-1)).to_bytes(width,'little'))
                else:
                    lookup(uc,UC_MEM_READ,address,width,0,None)
            elif slot:
                base,read,write=slot
                assert write is not None,'write to read-only registers'
                value=uc.reg_read(register(target))&((1<<(width*8))-1)
                write(uc,address-base,width,value,None)
        try:
            u.emu_start''')
# No semantic change to assertions or timing. Only register access interception.
m=types.ModuleType('instruction_mapped');exec(compile(text,'instruction_mapped','exec'),m.__dict__)
Original=m.Runner
class Runner(Original):
 def __init__(self,elf,library,mmio_backend='instruction'):
  assert mmio_backend=='instruction'
  super().__init__(elf,library,mmio_backend)
m.Runner=Runner
