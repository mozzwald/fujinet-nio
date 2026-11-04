#!/usr/bin/env python3
"""Record the first invalid-lookup assertion context without weakening it."""
import argparse,json,types
from pathlib import Path
from instruction_harness import text
p=argparse.ArgumentParser();p.add_argument('elf',type=Path);p.add_argument('library',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();assert not a.output.exists()
needle="                assert samples and samples[-1]['live'], 'invalid address used for lookup'"
assert text.count(needle)==1
text=text.replace(needle,"""                if not samples or not samples[-1]['live']:
                    violations.append(dict(clock=self.lib.sb_cs1_co_clock(),owner=owner,address=f'{address:08x}',sample=samples[-1] if samples else None))
"""+needle)
m=types.ModuleType('record_invalid');exec(compile(text,'record_invalid','exec'),m.__dict__);m.violations=[]
try:
 m.Runner(a.elf,a.library,'instruction').run([(100,164,8,m.READ0,0),(170,234,2,m.READ1,0),(700,800,10,m.READ0,0)],profile=1,core0_period=0,stalls=[(100,500)])
except AssertionError as e:
 assert str(e)=='invalid address used for lookup'
 assert m.violations
 a.output.write_text(json.dumps(dict(result='BLOCKED as required',violations=m.violations,assertion=str(e)),indent=2)+'\n')
 print(json.dumps(m.violations[0]))
else:raise AssertionError('negative case unexpectedly passed')
