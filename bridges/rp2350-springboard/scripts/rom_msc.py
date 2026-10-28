#!/usr/bin/env python3
"""ROM drive preparation/inspection. Never reboots, flashes, or resets hardware.

Copy SPRINGBD.ROM normally, then flush/unmount. Import happens at RP startup.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import zlib


def validate_raw(raw):
    if not 512 <= len(raw) <= 65536 or len(raw) % 2:
        raise ValueError('ROM must have even length 512..65536')
    if raw[8:12] != bytes.fromhex('feedbeef') or raw[12:14] != b'\0\4' or raw[256:260] != bytes.fromhex('feedface'):
        raise ValueError('complete Palm Springboard ROM required, not a PRC')
    if int.from_bytes(raw[108:112], 'big') != len(raw):
        raise ValueError('ROM checksum length')
    import binascii
    crc = binascii.crc_hqx(raw[:112], 0)
    crc = binascii.crc_hqx(raw[114:], crc)
    if crc != int.from_bytes(raw[112:114], 'big'):
        raise ValueError('Palm card CRC16')


def package(raw, version):
    validate_raw(raw)
    if not 1 <= version <= 0xffffffff:
        raise ValueError('image version must be 1..0xffffffff')
    header = b'FNRM' + struct.pack('<6I', 1, version, len(raw), zlib.crc32(raw), 0, 0)
    return header + struct.pack('<I', zlib.crc32(header)) + raw


def inspect(data):
    if len(data) < 32:
        raise ValueError('short package')
    magic, schema, version, n, crc, a, b, hcrc = struct.unpack_from('<4s7I', data)
    if magic != b'FNRM' or schema != 1 or not version or a or b or hcrc != zlib.crc32(data[:28]):
        raise ValueError('package header')
    raw = data[32:]
    if len(raw) != n or zlib.crc32(raw) != crc:
        raise ValueError('package payload')
    validate_raw(raw)
    return dict(version=version, payload_length=n, payload_crc32=f'{crc:08x}',
                payload_sha256=hashlib.sha256(raw).hexdigest(), file_sha256=hashlib.sha256(data).hexdigest())


def check_port(port, ports):
    """Refuse legacy consoles before opening/sending even one command byte."""
    target = Path(port).resolve()
    matches = [p for p in ports if Path(p.device).resolve() == target]
    if len(matches) != 1 or (matches[0].vid, matches[0].pid, matches[0].product) != (0xcafe, 0x4013, 'FujiNet ROM Drive v1'):
        raise ValueError('refusing unexpected USB identity; ROM-drive CDC required')
    return matches[0].device


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    pack = sub.add_parser('pack')
    pack.add_argument('rom', type=Path)
    pack.add_argument('output', type=Path)
    pack.add_argument('--version', type=lambda s: int(s, 0), required=True)
    pack.add_argument('--manifest', type=Path, required=True, help='independent Palm ROM verification manifest')
    info = sub.add_parser('inspect'); info.add_argument('file', type=Path)
    copy = sub.add_parser('copy'); copy.add_argument('file', type=Path); copy.add_argument('mountpoint', type=Path)
    status = sub.add_parser('status'); status.add_argument('port', help='explicit CDC tty path')
    a = p.parse_args()
    if a.command == 'pack':
        raw = a.rom.read_bytes(); m = json.loads(a.manifest.read_text())
        if m.get('size') != len(raw) or m.get('sha256') != hashlib.sha256(raw).hexdigest():
            p.error('ROM differs from independent manifest')
        data = package(raw, a.version)
        with a.output.open('xb') as f:
            f.write(data)
        print(json.dumps(inspect(data), indent=2))
    elif a.command == 'inspect':
        print(json.dumps(inspect(a.file.read_bytes()), indent=2))
    elif a.command == 'copy':
        data = a.file.read_bytes(); report = inspect(data)
        if not os.path.ismount(a.mountpoint):
            p.error('destination must be an explicitly mounted ROM drive')
        # Filesystem owns allocation and fragmentation. No raw block writes.
        target = a.mountpoint / 'SPRINGBD.ROM'
        with target.open('wb') as f:
            f.write(data); f.flush(); os.fsync(f.fileno())
        if target.read_bytes() != data:
            raise ValueError('copied file readback mismatch')
        report.update(copied=str(target), activated=False, next='Unmount/eject, then controlled RP restart; no restart performed.')
        print(json.dumps(report, indent=2))
    else:
        import serial
        from serial.tools import list_ports
        device = check_port(a.port, list_ports.comports())
        with serial.Serial(device, 115200, timeout=3, write_timeout=3) as port:
            check_port(device, list_ports.comports())
            port.reset_input_buffer(); port.write(b'status\n'); port.flush()
            response = json.loads(port.readline())
        if response.get('product') != 'FujiNet ROM Drive v1':
            raise ValueError('unexpected firmware identity')
        print(json.dumps(response, indent=2))

if __name__ == '__main__':
    main()
