#!/usr/bin/env python3
"""Enumerate actual linked code, literal and SRAM differences from msc-003."""
import argparse,json,re,subprocess,hashlib,importlib.util
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('baseline',type=Path);p.add_argument('candidate',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2];spec=importlib.util.spec_from_file_location('elfcheck',root.parents[2]/'fujinet-palm/tools/verify-ramsetup-firmware.py');v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
b=v.ELF(a.baseline.read_bytes());n=v.ELF(a.candidate.read_bytes())
def listing(path,elf,name):
 addr,size,_,_=elf.symbols[name];addr&=~1
 dis=subprocess.check_output(['arm-none-eabi-objdump','-d',str(path)],text=True);ops=[];literals=[];hooks=0
 for m in re.finditer(r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{4,8}\s+)+(.+)$',dis,re.M):
  if not addr<=int(m[1],16)<addr+size:continue
  text=m[2].split('@')[0].strip()
  if text.startswith(('.word','.short','.byte')):literals.append(text);continue
  if '<__wrap_sb_pio_rearm_program_init>' in text:hooks+=1;text=text.replace('<__wrap_sb_pio_rearm_program_init>','<sb_pio_rearm_program_init>')
  text=re.sub(r'\b[0-9a-f]+\s+(<[^>]+>)',r'\1',text)
  ops.append(text)
 return ops,literals,hooks
report={'result':'PASS','code':{},'symbol_moves':{},'new_symbols':sorted(set(n.symbols)-set(b.symbols)),'limits':'symbolic instruction comparison excludes literal-data addresses; explicit raw literal changes and all symbol moves are recorded; not a physical timing proof'}
for name in ('main','service','initialize_usb','install_ram_exceptions','sb_msc_fault_park','sb_boot_autoarm_entry','sb_persistent_reply','sb_pio_rearm_program_init'):
 x,xl,_=listing(a.baseline,b,name);y,yl,h=listing(a.candidate,n,name)
 assert x==y,('instruction sequence changed',name,[(i,u,w) for i,(u,w) in enumerate(zip(x,y)) if u!=w][:5])
 report['code'][name]={'same_instruction_sequence':True,'instructions':len(x),'expected_init_hook_sites':h,'baseline_address':hex(b.symbols[name][0]),'candidate_address':hex(n.symbols[name][0]),'same_stored_bytes':b.symbol(name)==n.symbol(name),'literal_changes':[dict(baseline=u,candidate=w) for u,w in zip(xl,yl) if u!=w]}
assert report['code']['service']['expected_init_hook_sites']==1
for name,old in b.symbols.items():
 if name in n.symbols and old[0]!=n.symbols[name][0]:report['symbol_moves'][name]={'from':hex(old[0]),'to':hex(n.symbols[name][0]),'delta':n.symbols[name][0]-old[0],'size':n.symbols[name][1]}
for name in ('words','bytes','import_buffer','disk','storage'):
 assert n.symbols[name][1]==b.symbols[name][1]
 assert n.symbols[name][0]-b.symbols[name][0]==240,(name,n.symbols[name],b.symbols[name])
assert n.symbols['words'][0]%16==b.symbols['words'][0]%16==8
report['static_buffers_shift']=240
report['source_identity']={}
for name in ('msc_main.c','pio_rearm_program.c','persistent_reply.S','boot_autoarm_entry.S'):
 report['source_identity'][name]=hashlib.sha256((root/'src'/name).read_bytes()).hexdigest()
a.output.write_text(json.dumps(report,indent=2)+'\n');print('PASS unchanged symbolic ROM/Core0/startup instruction sequences, one init-call redirect; 240-byte static buffer shift recorded')
