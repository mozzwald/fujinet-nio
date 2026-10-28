#!/usr/bin/env python3
"""Decode bounded diagnostic records from the existing serial palm-sync PDB pull."""
import argparse,json,struct,zlib
from pathlib import Path
FIELDS='magic version sequence mode error card generation cs0 cs_size cs1 access_ns ram_pc requested completed cs0_reads cs1_reads value mismatches first_expected first_actual ram_proof ticks seconds crc32'.split()
def decode_record(raw):
 if len(raw)!=96:raise ValueError('record length')
 values=struct.unpack('>24I',raw);r=dict(zip(FIELDS,values))
 if r['magic']!=0x46444632 or r['version']!=1:raise ValueError('record identity')
 if zlib.crc32(raw[:92])!=r['crc32']:raise ValueError('record CRC')
 if r['mode'] not in (1,2,3,4):raise ValueError('mode')
 expected_counts={1:(1,0,1),2:(100,0,100),3:(100,100,100),4:(4,8,0)}[r['mode']]
 requested,c0,c1=expected_counts
 r['pass']=bool(not r['error'] and not r['mismatches'] and r['ram_proof']==1 and r['card']>0 and r['generation']>0 and r['cs_size']==0x1000000 and r['cs0'] and not r['cs0']&0xffffff and r['cs1']==r['cs0']+r['cs_size']<=0xffffffff and r['access_ns']==330 and r['requested']==r['completed']==requested and r['cs0_reads']==c0 and r['cs1_reads']==c1 and r['value']==(0xfeedbeef if r['mode']==4 else 0xbeef))
 return r
def pdb_records(data):
 if len(data)<78 or data[:32].split(b'\0')[0]!=b'FN Fixed Read E2 Log' or data[60:68]!=b'DATAFNFR':raise ValueError('PDB identity')
 if int.from_bytes(data[32:34],'big')&1:raise ValueError('resource DB')
 count=int.from_bytes(data[76:78],'big');end=78+8*count
 if len(data)<end:raise ValueError('record table')
 offsets=[int.from_bytes(data[78+i*8:82+i*8],'big') for i in range(count)]
 if any(o<end or o>=len(data) for o in offsets) or offsets!=sorted(set(offsets)):raise ValueError('record offsets')
 return [data[o:offsets[i+1] if i+1<count else len(data)] for i,o in enumerate(offsets)]
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('pdb',type=Path);a=p.parse_args();print(json.dumps([decode_record(x) for x in pdb_records(a.pdb.read_bytes())],indent=2))
