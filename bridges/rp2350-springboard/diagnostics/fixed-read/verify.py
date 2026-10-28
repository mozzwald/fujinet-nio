#!/usr/bin/env python3
"""Independent PRC metadata/opcode checks; does not claim Palm execution."""
import hashlib,json,struct
from pathlib import Path
p=Path(__file__).resolve().parent;raw=(p/'build/cs1-fixed-e2.prc').read_bytes();assert raw[:32].split(b'\0')[0]==b'CS1 Fixed E2';assert raw[60:68]==b'applFNFR';assert int.from_bytes(raw[32:34],'big')==1
n=int.from_bytes(raw[76:78],'big');entries=[struct.unpack_from('>4sHI',raw,78+i*10) for i in range(n)];resources={}
assert all(78+n*10<=o<len(raw) for t,k,o in entries) and all(a[2]<b[2] for a,b in zip(entries,entries[1:]))
for i,(t,k,o) in enumerate(entries):resources[t,k]=raw[o:entries[i+1][2] if i+1<n else len(raw)]
code=resources[b'code',1]
for signature in ('206f0004700030104e75','206f000420104e75','41fafffe20084e75'):assert code.count(bytes.fromhex(signature))==1
assert b'E2.20261028.1' in resources[b'tver',1]
dis=(p/'build/disassembly.txt').read_text();assert '<df_read_word>:' in dis and 'movew %a0@,%d0' in dis;assert '<df_read_pair>:' in dis and 'movel %a0@,%d0' in dis
r=dict(result='PASS',name='CS1 Fixed E2',creator='FNFR',type='appl',attributes=1,prc_bytes=len(raw),prc_sha256=hashlib.sha256(raw).hexdigest(),code1_sha256=hashlib.sha256(code).hexdigest(),explicit_move_w=True,explicit_rom_move_l=True,pc_relative_ram_location_probe=True,hardware_tested=False)
(p/'build/verification.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
