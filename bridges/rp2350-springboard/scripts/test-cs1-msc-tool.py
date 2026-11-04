#!/usr/bin/env python3
"""Offline CS1 host preflight and stale-image/ACK refusal."""
import importlib.util,json,hashlib,tempfile,copy
from pathlib import Path
from types import SimpleNamespace
s=importlib.util.spec_from_file_location('cs1',Path(__file__).with_name('cs1_msc.py'));m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
port=SimpleNamespace(device='/tmp/no-real-tty',vid=m.VID,pid=m.PID,product=m.PRODUCT)
assert m.check_port(port.device,[port])==port.device
for rows in ([],[port,port],[SimpleNamespace(device=port.device,vid=0xcafe,pid=0x4013,product='FujiNet ROM Drive v1')]):
 try:m.check_port(port.device,rows)
 except ValueError:pass
 else:raise AssertionError('wrong/ambiguous console accepted')
b={'build_id':'a'*64,'rom_length':8842,'rom_crc32':'5228d802','rom_version':2}
r=dict(product=m.PRODUCT,build_id=b['build_id'],served_length=8842,served_crc32=b['rom_crc32'],served_version=2,import_result=0,clock_khz=150000,ram_island=1,flash_size_ok=1,exception_fault=0,disk_fault=0,active=1,
 cs1=dict(id=0x464e,abi=0x100,capabilities=1,status=1,ack=0,accepted=0,capture_fault=0,error=0,read_fault=0,stopped=0,arm_pending=0))
m.validate(r,b,ready=True)
for field,value in (('build_id','b'*64),('served_length',6758),('served_version',1),('served_crc32','838f2520'),('import_result',1),('ram_island',0),('disk_fault',1),('active',0),('clock_khz',120000)):
 x=copy.deepcopy(r);x[field]=value
 try:m.validate(x,b,ready=True)
 except ValueError:pass
 else:raise AssertionError(field)
for field,value in (('status',24),('ack',1),('accepted',1),('capture_fault',1),('error',7),('read_fault',1),('stopped',1),('arm_pending',1)):
 x=copy.deepcopy(r);x['cs1'][field]=value
 try:m.validate(x,b,ready=True)
 except ValueError:pass
 else:raise AssertionError(field)
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'image').write_bytes(b'image');b.update(kind='cs1-msc-stage-a',artifacts={'uf2':{'path':'image','sha256':hashlib.sha256(b'image').hexdigest()}});(p/'bundle.json').write_text(json.dumps(b));m.verify_bundle(p/'bundle.json')
 (p/'image').write_bytes(b'wrong')
 try:m.verify_bundle(p/'bundle.json')
 except ValueError:pass
 else:raise AssertionError('bad artifact accepted')
print('PASS identity, artifact hashes, stale ROM/ACK, fault and fresh-session gates')
