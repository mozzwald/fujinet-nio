#!/usr/bin/env python3
"""Offline host-tool safety tests; no tty/device access."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
s=importlib.util.spec_from_file_location('rom_msc',Path(__file__).with_name('rom_msc.py'));m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
port=SimpleNamespace(device='/tmp/not-a-real-rom-drive-tty',vid=0xcafe,pid=0x4013,product='FujiNet ROM Drive v1')
assert m.check_port(port.device,[port])==port.device
for wrong in ([],[SimpleNamespace(device=port.device,vid=0x2e8a,pid=9,product='Pico')],
              [SimpleNamespace(device=port.device,vid=port.vid,pid=port.pid,product='other')],[port,port]):
 try:m.check_port(port.device,wrong)
 except ValueError:pass
 else:raise AssertionError('unexpected/ambiguous identity accepted')
for version in (0,0x100000000):
 try:m.package(bytes(512),version)
 except ValueError:pass
 else:raise AssertionError('invalid image accepted')
for file in (b'',b'PRC'+bytes(600),b'FNRM'+bytes(28)):
 try:m.inspect(file)
 except ValueError:pass
 else:raise AssertionError('invalid file accepted')
print('PASS host-tool device identity preflight; legacy/ambiguous ports and invalid files refused')
