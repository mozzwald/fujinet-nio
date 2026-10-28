#!/usr/bin/env python3
"""Report actual linked Core1 direct call graph and per-function stack usage."""
import argparse,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('elf',type=Path);p.add_argument('build',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
text=subprocess.check_output(['arm-none-eabi-objdump','-d',str(a.elf)],text=True)
functions={};current=None
for line in text.splitlines():
    m=re.match(r'^([0-9a-f]+) <([^>]+)>:',line)
    if m:current=m[2];functions[current]=dict(address=int(m[1],16),calls=[],indirect=[])
    elif current:
        m=re.search(r'\b(?:bl|blx|b\.w)\s+([0-9a-f]+) <([^>]+)>',line)
        if m:
            name=m[2].split('+')[0]
            if name!=current:functions[current]['calls'].append(name)
        elif re.search(r'\bblx\s+r\d+',line):functions[current]['indirect'].append(line.strip())
seen=set();pending=['service','sb_pio_rearm_program_init']
while pending:
    name=pending.pop()
    if name in seen:continue
    seen.add(name);f=functions[name];assert 0x20000000<=f['address']<0x20082000,(name,f)
    assert not f['indirect'],(name,f['indirect'])
    pending.extend(f['calls'])
# objdump may prefer one alias label. The shared fault handler was verified
# separately by address and actual mapped execution.
stack={}
for f in a.build.rglob('*.su'):
    for line in f.read_text().splitlines():
        columns=line.split('\t')
        if len(columns)>=3:stack[columns[0].split(':')[-1]]=dict(bytes=int(columns[1]),kind=columns[2])
for name in ('service','ms_write','ms_transfer','mi_load','main','flash_range_program'):
    assert name in stack,name
# Record the known active write chain (exclusive of ROM routine internals and
# IRQ/exception nesting). Do not label it a complete worst-case stack proof.
chain=['main','tud_task_ext','mscd_xfer_cb','tud_msc_write10_cb','ms_transfer','ms_write','pg','flash_range_program']
subtotal=sum(stack.get(n,{}).get('bytes',0) for n in chain)
assert subtotal<3072,(subtotal,chain)
a.output.write_text(json.dumps(dict(result='PASS',core1_direct_call_graph={n:functions[n] for n in sorted(seen)},
    core1_policy_indirect_target='sb_boot_policy.service_entry == service; independently verified startup execution',
    write_chain=chain,write_chain_known_stack_subtotal=subtotal,stack_usage=stack,
    stack_limit_per_core=4096,limitations='ROM routines, indirect Core0 SDK calls, nested IRQ/fault stack and actual arbitration require separate audit/physical qualification; static SRAM residency is not analog timing proof.'),indent=2)+'\n')
print('PASS Core1 direct call graph has no XIP or unresolved indirect calls; stack-usage report archived')
