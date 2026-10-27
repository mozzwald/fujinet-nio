#!/usr/bin/env python3
"""Expanded compiled ARM/PIO scheduling tests; each scenario is process-isolated.

Requires the same Unicorn/pyelftools environment as test-cs1-engine.py.
The original matrix remains separate and mandatory. No device is opened.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import gzip
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

spec = importlib.util.spec_from_file_location('engine',Path(__file__).with_name('test-cs1-engine.py'))
engine = importlib.util.module_from_spec(spec)
spec.loader.exec_module(engine)
R,I,A,B,W = engine.RESET,engine.IDLE,engine.READ0,engine.READ1,engine.WRITE


def scenarios():
    cases = []
    def add(name,schedule,*,regs=None,**opts):
        cases.append(dict(name=name,schedule=schedule,regs=regs or {},**opts))
    for phase in range(8):
        for gap in (5,6,7):
            t=100+phase
            for reader in (A,B):
                add(f'write-read-p{phase}-g{gap}-cs{int(reader==B)}',
                    [(t,t+64,4,W | (0x1234<<20),0),
                     (t+64+gap,t+128+gap,8 if reader==A else 4,reader,0x1234)],
                    regs=dict(scratch=0x1234,captured=1,committed=1,next_epoch=0xffffffff))
            add(f'alternate-p{phase}-g{gap}',[
                (t+i*(64+gap),t+i*(64+gap)+64,(8 if i%2==0 else 2),A if i%2==0 else B,0)
                for i in range(8)])
            add(f'cs1-p{phase}-g{gap}',[
                (t+i*(64+gap),t+i*(64+gap)+64,2*(i%4),B,0) for i in range(8)])
    add('cancel-then-rom',[(100,164,4,W|(0x1234<<20),0),
                         (164,180,4,R|(7<<16)|(0x1234<<20),0),
                         (186,250,8,A,0)],
        regs=dict(scratch=0,captured=1,committed=0,rejected=1,next_epoch=0xffffffff))
    add('cancel-valid-read',[(100,164,4,W|(0x1234<<20),0),
                           (164,180,4,R|(7<<16)|(0x1234<<20),0),
                           (420,484,4,W|(0x5c8f<<20),0),
                           (680,744,4,B,0x5c8f)],
        regs=dict(scratch=0x5c8f,captured=2,committed=1,rejected=1,next_epoch=0xfffffffe))
    add('two-queued-writes',[(100,164,4,W|(0x1234<<20),0),
                             (170,234,4,W|(0x5c8f<<20),0),
                             (900,1028,4,B,0x5c8f)],stalls=[(80,400)],
        regs=dict(scratch=0x5c8f,captured=2,committed=2,next_epoch=0xfffffffe))
    add('overflow-quarantine',[(100+70*i,164+70*i,4,W|((0x1234+i)<<20),0) for i in range(3)],
        stalls=[(80,500)],fault=1,regs=dict(scratch=0,fault=5,captured=0,committed=0,rejected=1))
    for name,pins in [('reset',15<<16),('select-conflict',R|(8<<16)),('oe-we',R|(1<<16))]:
        add(f'sticky-{name}',[(100,164,4,W|(0x1234<<20),0),
                             (190,222,0,pins,0)],stalls=[(80,300)],fault=1,
            regs=dict(scratch=0,fault=8,captured=0,committed=0,rejected=1))
    for kind in range(4):
        add(f'injected-sticky-{kind}',[(100,164,4,W|(0x1234<<20),0)],
            stalls=[(80,250)],injections=[(190,kind)],fault=1,
            regs=dict(scratch=0,fault=8 if kind==3 else 5,captured=0,committed=0,rejected=1))
    add('late-before-address',[(100,164,8,A,0),(420,484,2,B,0)],
        stalls=[(110,240)],drive=[False,True],attempted=1,cancelled=1)
    add('late-after-address',[(100,164,8,A,0),(420,484,2,B,0)],
        stalls=[(130,250)],drive=[False,True],attempted=1,cancelled=1)
    add('release-cpu-stalled',[(100,164,8,A,0),(420,484,2,B,0)],stalls=[(155,350)])
    for count in (0x3fffffff,0x7fffffff,0xbfffffff,0xffffffff):
        add(f'completion-carry-{count:08x}',[(100,164,8,A,0),(420,484,2,B,0)],
            initial_completed=count)
    return cases


def metrics(result):
    tr = result['trace']
    polls=[i for i,t in enumerate(tr) if '[r3, #364]' in t['instruction'] or '[r3, #4]' in t['instruction']]
    gaps=[(tr[b]['clock']-tr[a]['clock'],a,b) for a,b in zip(polls,polls[1:])]
    longest=max(gaps,default=(0,0,0))
    gap,a,b=longest
    return dict(max_read_poll_gap=gap,uninterrupted_instruction_count=b-a,
                from_pc=tr[a]['pc'] if tr else None,to_pc=tr[b]['pc'] if tr else None,
                invalid_address_samples=[e for e in result['events'] if e['kind']=='SIO' and not e['live']])


def worker(args,case):
    out=args.output;out.mkdir(parents=True,exist_ok=True)
    runner=engine.Runner(args.elf,args.library)
    result=runner.run(case['schedule'],profile=args.profile,trace_file=out/'instructions.trace',
                      stalls=case.get('stalls',()),injections=case.get('injections',()),
                      initial_completed=case.get('initial_completed',0))
    reasons=[]
    expected_reads=sum(pins in (A,B) for _,_,_,pins,_ in case['schedule'])
    expected_drive=case.get('drive',[True]*expected_reads)
    expected_totals=dict(requests=expected_reads,posted=expected_reads,completed=expected_reads,
                         attempted=case.get('attempted',expected_reads),cancelled=case.get('cancelled',0),
                         fault=case.get('fault',0))
    for k in ('requests','posted','completed','attempted'):
        expected_totals[k]=(expected_totals[k]+case.get('initial_completed',0)) & 0xffffffff
    for k,v in expected_totals.items():
        if result['totals'][k]!=v: reasons.append(f'{k}: {result["totals"][k]} != {v}')
    if result['enables']!=case.get('attempted',expected_reads): reasons.append('enable count')
    if [w['driven_before_end'] for w in result['read_windows']]!=expected_drive: reasons.append('read window validity')
    for k,v in case['regs'].items():
        if result['register_state'][k]!=v: reasons.append(f'register {k}: {result["register_state"][k]} != {v}')
    result['latency']=metrics(result)
    # A late probe is permitted only if Runner proves no lookup and an
    # explicitly cancelled owned dummy. The user clarified this contract.
    result['passed']=not reasons
    result['reasons']=reasons
    raw=json.dumps(result).encode()
    with open(out/'full-trace.json.gz','wb') as stream:
        with gzip.GzipFile(filename='',mode='wb',fileobj=stream,mtime=0) as zipped:zipped.write(raw)
    del result['trace']
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    # Readable line trace remains essential on native assertion aborts. On
    # completed runs the compressed trace includes exactly the same records.
    (out/'instructions.trace').unlink()
    return 0 if result['passed'] else 1


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('elf',type=Path);p.add_argument('library',type=Path)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--worker');p.add_argument('--profile',type=int,choices=(1,2))
    p.add_argument('--jobs',type=int,default=4)
    args=p.parse_args()
    cases=scenarios()
    if args.worker:
        case=next(c for c in cases if c['name']==args.worker)
        return worker(args,case)
    args.output.mkdir(parents=True,exist_ok=True)
    def run(item):
        case,profile=item
        out=args.output/f'{case["name"]}-p{profile}'
        out.mkdir(exist_ok=True)
        cmd=[sys.executable,str(Path(__file__).resolve()),str(args.elf.resolve()),str(args.library.resolve()),
             '--output',str(out.resolve()),'--worker',case['name'],'--profile',str(profile)]
        with (out/'worker.txt').open('w') as log:
            proc=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT)
        path=out/'result.json'
        result=json.loads(path.read_text()) if path.exists() else dict(passed=False,reasons=['native/ARM execution aborted'])
        return f'{case["name"]}/p{profile}',dict(exit_status=proc.returncode,passed=result['passed'],
                                                    reasons=result.get('reasons',[]),latency=result.get('latency'))
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results=dict(pool.map(run,[(c,profile) for c in cases for profile in (1,2)]))
    (args.output/'summary.json').write_text(json.dumps(results,indent=2)+'\n')
    passed=sum(r['passed'] for r in results.values())
    print(f'Expanded ARM/PIO: {passed}/{len(results)} PASS; all failures retained in {args.output}')
    return int(passed!=len(results))

if __name__=='__main__':sys.exit(main())
