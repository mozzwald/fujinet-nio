"""Mapped persistent-ROM harness with fixed-CS1 expectations; no device access."""
import json,types
from pathlib import Path
source=Path(__file__).with_name('test-persistent-reply.py').read_text()
def replace(old,new):
 global source
 assert source.count(old)==1,old
 source=source.replace(old,new,1)
replace('expected_word = 0xffff if saved_high & 0x1f700000 else word(low & 0xfffe)','expected_word = 0xbeef if saved_high & 15 == 9 else 0xffff if saved_high & 0x1f700000 else word(low & 0xfffe)')
replace('valid = saved_high & 0x2000000f == 0x2000000a','valid = saved_high & 0x2000000f in (0x2000000a,0x20000009)')
replace('ctrl & (15 << 16) == 10 << 16','ctrl & (15 << 16) in (10 << 16,9 << 16)')
replace("e['value'] & 0x2000000f == 0x2000000a","e['value'] & 0x2000000f in (0x2000000a,0x20000009)")
replace('''        u.mmio_map(PIO, 0x1000, read, None, write, None)
        u.mmio_map(0xd0000000, 0x1000, sio_read, None, None, None)''','''        def mapped(base, reader, writer=None):
            u.mem_map(base,0x1000)
            def rd(uc,access,address,size,value,_):
                uc.mem_write(address,int(reader(uc,address-base,size,None)).to_bytes(size,'little'))
            def wr(uc,access,address,size,value,_):
                assert writer is not None
                writer(uc,address-base,size,value,None)
            u.hook_add(UC_HOOK_MEM_READ,rd,begin=base,end=base+0xfff)
            u.hook_add(UC_HOOK_MEM_WRITE,wr,begin=base,end=base+0xfff)
        mapped(PIO,read,write)
        mapped(0xd0000000,sio_read)''')
source=source.replace('actual linked ARM instructions + unchanged PIO','actual linked ARM instructions + candidate shared PIO')
m=types.ModuleType('dual_fixed');exec(compile('from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE\n'+source,'dual_fixed','exec'),m.__dict__)
