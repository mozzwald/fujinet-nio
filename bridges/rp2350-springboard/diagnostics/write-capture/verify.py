#!/usr/bin/env python3
"""Independent metadata and actual 68000 opcode check; no device access."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

p = argparse.ArgumentParser(); p.add_argument('--output', type=Path, required=True); a = p.parse_args()
root = Path(__file__).resolve().parent
raw = (root / 'build/cs1-write-e3.prc').read_bytes()
assert raw[:32].split(b'\0')[0] == b'CS1 Write E3'
assert raw[60:68] == b'applFNFW' and int.from_bytes(raw[32:34], 'big') == 1
n = int.from_bytes(raw[76:78], 'big')
entries = [struct.unpack_from('>4sHI', raw, 78+i*10) for i in range(n)]
assert all(78+n*10 <= o < len(raw) for t, k, o in entries)
assert all(x[2] < y[2] for x, y in zip(entries, entries[1:]))
r = {(t, k): raw[o:entries[i+1][2] if i+1 < n else len(raw)] for i, (t, k, o) in enumerate(entries)}
code = r[b'code', 1]
assert code.count(bytes.fromhex('206f000430af00084e75')) == 1
assert code.count(bytes.fromhex('41fafffe20084e75')) == 1
assert b'E3.20261028.1' in r[b'tver', 1]
dis = (root / 'build/disassembly.txt').read_text()
assert '<fw_write_word>:' in dis and 'movew %sp@(8),%a0@' in dis
report = dict(result='PASS', name='CS1 Write E3', creator='FNFW', type='appl', attributes=1,
              bytes=len(raw), prc_sha256=hashlib.sha256(raw).hexdigest(),
              code1_sha256=hashlib.sha256(code).hexdigest(),
              explicit_move_w_write=True, pc_relative_ram_probe=True,
              no_reset_attribute=True, hardware_tested=False)
a.output.write_text(json.dumps(report, indent=2) + '\n'); print(json.dumps(report, indent=2))
