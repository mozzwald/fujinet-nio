"""Actual ARM initializer write audit, no startup and no hardware access."""
import argparse,hashlib,importlib.util,json
from pathlib import Path
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('baseline',type=Path);p.add_argument('candidate',type=Path);p.add_argument('--baseline-proof',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
assert not args.output.exists()
assert hashlib.sha256(args.baseline.read_bytes()).hexdigest()==json.loads(args.baseline_proof.read_text())['elf_sha256']
s=importlib.util.spec_from_file_location('init',root/'scripts/test-cs0-passive-initializer.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
b=m.execute(args.baseline,'sb_pio_rearm_program_init')
a=m.execute(args.candidate,'sb_cs1_captured_init')
assert a['writes'][:len(b['writes'])]==b['writes'],'original initializer write prefix changed'
assert a['instruction_ram'][1]==b['instruction_ram'][1]
assert a['instruction_ram'][0][:3]==b['instruction_ram'][0][:3]
extra=a['writes'][len(b['writes']):]
# Added PIO0 instruction words3..10 or registers belonging only to SM2/3.
assert all((0x50200054<=int(w['address'],16)<=0x50200070 or 0x502000f8<=int(w['address'],16)<=0x50200124) for w in extra),extra
out=dict(result='PASS',scope='actual no-startup ARM initializer, original writes retained as exact prefix; no original PIO0 SM0/1, PIO1 or GPIO changes',baseline=b,candidate=a,added_pio0_writes=extra)
args.output.write_text(json.dumps(out,indent=2)+'\n')
print('PASS actual initializer; exact baseline prefix plus',len(extra),'isolated PIO0 writes')
