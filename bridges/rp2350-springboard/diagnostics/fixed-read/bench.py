#!/usr/bin/env python3
"""Explicit manual bench utility; never flashes, resets or activates RP firmware.
No hardware access until the user invokes install or collect. Serial palm-sync only.
"""
import argparse,importlib.util,os,subprocess,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('action',choices=['install','collect']);p.add_argument('--output',type=Path,required=True);p.add_argument('--prc',type=Path);p.add_argument('--timeout',type=int,default=900);a=p.parse_args()
base=Path(__file__).resolve().parent;tools=base.parents[4]/'fujinet-palm/tools';spec=importlib.util.spec_from_file_location('prism',tools/'prism_collect.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=False)
if a.action=='install':
 if not a.prc:p.error('--prc required for install')
 raw=a.prc.read_bytes()
 if raw[:32].split(b'\0')[0]!=b'CS1 Fixed E2' or raw[60:68]!=b'applFNFR' or int.from_bytes(raw[32:34],'big')!=1:p.error('unexpected PRC identity/flags')
 m.transfer(a.output,a.output,'--install',[a.prc.resolve()],a.timeout,transport='palm-sync')
else:
 port=m.wait_palm(a.timeout);command=['node',str(tools/'palm-sync/dist/bin/cli.js'),'run',str(base/'collect.js'),'--serial',port];env=dict(os.environ,DF_RESULT_DIR=str(a.output))
 (a.output/'command.json').write_text(json.dumps(command,indent=2)+'\n')
 with (a.output/'transport.log').open('x') as f:subprocess.run(command,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=300,check=True)
 from decode import decode_record,pdb_records
 result=[decode_record(raw) for raw in pdb_records((a.output/'results.pdb').read_bytes())]
 (a.output/'decoded.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
