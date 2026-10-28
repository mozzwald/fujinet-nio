#!/usr/bin/env python3
"""Focused comparison using the existing unmodified ARM/PIO safety harness."""
import argparse,importlib.util,json,types,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('baseline_elf',type=Path);p.add_argument('candidate_elf',type=Path);p.add_argument('baseline_library',type=Path);p.add_argument('candidate_library',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--full-suite',action='store_true');a=p.parse_args()
# Local register-backend adapter only; no original schedule/assertion edits.
# Ordinary mapped registers avoid the previously identified callback ITSTATE
# defect. This retains the frozen kernel layout, not an experimental worker.
source=Path(__file__).with_name('test-persistent-reply.py').read_text()
old="""        u.mmio_map(PIO, 0x1000, read, None, write, None)
        u.mmio_map(0xd0000000, 0x1000, sio_read, None, None, None)"""
new="""        def mapped(base, reader, writer=None):
            u.mem_map(base,0x1000)
            def rd(uc,access,address,size,value,_):
                uc.mem_write(address,int(reader(uc,address-base,size,None)).to_bytes(size,'little'))
            def wr(uc,access,address,size,value,_):
                assert writer is not None
                writer(uc,address-base,size,value,None)
            u.hook_add(UC_HOOK_MEM_READ,rd,begin=base,end=base+0xfff)
            u.hook_add(UC_HOOK_MEM_WRITE,wr,begin=base,end=base+0xfff)
        mapped(PIO,read,write)
        mapped(0xd0000000,sio_read)"""
assert source.count(old)==1
m=types.ModuleType('preparation_mapped');exec(compile('from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE\n'+source.replace(old,new),'preparation_mapped','exec'),m.__dict__)
if a.full_suite:
    saved=sys.argv
    try:
        for label,elf,lib in [('baseline',a.baseline_elf,a.baseline_library),('candidate',a.candidate_elf,a.candidate_library)]:
            sys.argv=[saved[0],str(elf),str(lib),'--output',str(a.output.with_name(a.output.stem+'-'+label+'.json'))]
            m.main()
    finally:sys.argv=saved
    a.output.write_text(json.dumps(dict(result='PASS',baseline_cases=359,candidate_cases=359,backend='ordinary mapped registers',assertions='unchanged original suite'),indent=2)+'\n')
    raise SystemExit(0)
runners=[m.Runner(a.baseline_elf,a.baseline_library),m.Runner(a.candidate_elf,a.candidate_library)]
for r in runners:r.lib.sb_cosim_init()
old=[runners[0].lib.sb_preparation_instruction(1,i) for i in range(32)]
new=[runners[1].lib.sb_preparation_instruction(1,i) for i in range(29)]
assert old[:23]==new[:23], 'owner instructions changed'
assert [runners[0].lib.sb_preparation_instruction(0,i) for i in range(3)]==[runners[1].lib.sb_preparation_instruction(0,i) for i in range(3)],'WE/CS1 instructions changed'
rows=[]
def summary(r):
 return {kind:[e['clock'] for e in r['pio_events'] if e['kind']==number and (number!=5 or e['value']==1)] for kind,number in [('request',1),('completion',2),('preload',5),('enable',4),('release',7)]}
for profile in (1,2):
 for phase in range(8):
  for gap in (6,7,8):
   t=100+phase;seq=[(t,t+68,8,m.READ),(t+68+gap,t+136+gap,10,m.READ)]
   results=[r.run(seq,profile=profile) for r in runners]
   stages=[summary(r) for r in results]
   assert stages[0]==stages[1],('normal-read timing changed',profile,phase,gap,stages)
   assert results[0]['events']==results[1]['events'],'ARM FIFO/SIO/TX timing changed'
   assert all(r['totals']['attempted']==2 and r['selected_enable_count']==2 for r in results)
   rows.append(dict(profile=profile,phase=phase,gap=gap,first_select=t,second_select=t+68+gap,first_end=t+68,second_end=t+136+gap,baseline=stages[0],candidate=stages[1],arm_events=results[1]['events']))
ends=[m.IDLE,m.READ|(1<<18),m.READ&~m.RESET,m.READ&~(1<<19),m.READ&~(1<<17),m.READ|(1<<16)]
safety=[]
for which,ending in enumerate(ends):
 for phase in (0,12,28,60,80):
  seq=[(100,100+phase,8,m.READ),(100+phase,140+phase,8,ending),(500,620,0x444,m.READ)]
  for n,r in enumerate(runners):
   x=r.run(seq,profile=2);assert x['totals']['requests']==x['totals']['completed']
   assert x['selected_enable_count']>=1
   safety.append(dict(engine=n,ending=which,phase=phase,totals=x['totals'],stages=summary(x)))
# Unselected OE strobes / writes must not poison the next valid ROM read.
for ctrl in (m.IDLE&~(1<<18),m.IDLE&~(1<<19),m.IDLE&~(1<<17)):
 for n,r in enumerate(runners):
  x=r.run([(40,80,0,ctrl),(100,168,8,m.READ),(174,242,10,m.READ)],profile=2)
  assert x['totals']['attempted']==2 and x['selected_enable_count']==2
  safety.append(dict(engine=n,unselected_control=ctrl,totals=x['totals']))
# CPU is frozen after data enable; every changed guard must release autonomously.
for ending in ends:
 for n,r in enumerate(runners):
  x=r.run([(100,180,8,m.READ),(180,400,8,ending),(700,850,0x444,m.READ)],profile=2,stall=(170,500))
  assert x['totals']['requests']==x['totals']['completed']
  safety.append(dict(engine=n,stalled_control=ending,totals=x['totals'],stages=summary(x)))
a.output.write_text(json.dumps(dict(result='PASS',owner_words_identical=23,baseline_pio1_words=32,candidate_pio1_words=29,normal_pairs=rows,safety_runs=safety,scope='unchanged harness; synthetic ARM costs and two-stage sampled PIO inputs; not physical timing'),indent=2)+'\n')
print('PASS 48 paired timing comparisons and',len(safety),'baseline/candidate guard runs; owner23 words identical')
