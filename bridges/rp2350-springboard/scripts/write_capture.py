#!/usr/bin/env python3
"""Experiment3 debug capture over existing CDC, not a FujiNet/NIO protocol.
Never flashes, writes a ROM, resets a processor or accesses Palm USB directly.
"""
import argparse
import json
from pathlib import Path


def verify_frame(r, session, address, value):
    required = ('low', 'low_end', 'high', 'high_end', 'outcome', 'pulses',
                'low_depth', 'high_depth', 'end_depth', 'address', 'value')
    for key in required:
        if type(r.get(key)) is not int or not 0 <= r[key] <= 0xffffffff:
            raise ValueError('missing/out-of-range ' + key)
    if (r.get('capture_abi') != 1 or r.get('session') != session or
            r.get('phase') != 2 or r.get('result') != 0 or
            r.get('qualified') != 1 or r.get('error') != ''):
        raise ValueError('capture not qualified for this session')
    if any(r.get(k) != 0 for k in ('reset_edge', 'cs0_edge', 'oe_edge', 'fifo_fault')):
        raise ValueError('capture contains invalidation/fault history')
    if (r['pulses'] != 1 or (r['low_depth'], r['high_depth'], r['end_depth']) != (2, 2, 1)
            or r['outcome'] or r['low_end'] not in (13, 15) or r['high_end'] not in (13, 15)):
        raise ValueError('pulse/FIFO/ending ownership mismatch')
    lo, hi = r['low'], r['high']
    if (lo >> 16) & 15 != 5 or hi & 15 != 5 or not hi & (1 << 29):
        raise ValueError('snapshots are not selected CS1 write/reset-released')
    actual_address = (lo & 0xfffe) | ((hi & 0x700000) >> 4) | ((hi & 0x1f000000) >> 5)
    actual_value = (hi >> 4) & 0xffff
    if (r['address'], r['value']) != (actual_address, actual_value):
        raise ValueError('decoded fields disagree with hardware snapshots')
    if (actual_address, actual_value) != (address, value):
        raise ValueError('captured address/data differs from intended Palm write')
    return dict(result='PASS', session=session, address=actual_address,
                value=actual_value, evidence='latched PIO snapshots and ending history',
                peripheral_command_committed=False)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=('status', 'arm', 'finish'))
    p.add_argument('--port', required=True)
    p.add_argument('--directory', type=Path, required=True)
    p.add_argument('--expected-address', type=lambda s: int(s, 0), default=0)
    p.add_argument('--expected-value', type=lambda s: int(s, 0), default=0x1234)
    a = p.parse_args()
    if a.expected_address & 1 or not 0 <= a.expected_address <= 0xfffffe or not 0 <= a.expected_value <= 0xffff:
        p.error('aligned24-bit offset and16-bit value required')
    import serial
    from serial.tools import list_ports
    from rom_msc import check_port
    device = check_port(a.port, list_ports.comports())

    def request(port, command):
        check_port(device, list_ports.comports())
        port.reset_input_buffer()
        port.write((command + '\n').encode('ascii')); port.flush()
        raw = port.readline()
        if not raw:
            raise RuntimeError('no response; do not automatically retry a state-changing command')
        return json.loads(raw)

    a.directory = a.directory.resolve()
    if a.action == 'arm':
        a.directory.mkdir(parents=True, exist_ok=False)
    elif not a.directory.is_dir():
        p.error('existing capture directory required')
    output = a.directory / (a.action + '.json')
    if output.exists():
        p.error('refusing to overwrite existing capture evidence')
    with serial.Serial(device, 115200, timeout=3, write_timeout=3) as port:
        base = request(port, 'status')
        if (base.get('product') != 'FujiNet ROM Drive v1' or not base.get('active') or
                any(base.get(k) != 0 for k in ('owner_fault', 'exception_fault', 'disk_fault'))):
            raise RuntimeError('ROM responder not active and fault-free')
        before = request(port, 'write-status')
        if before.get('capture_abi') != 1:
            raise RuntimeError('unexpected capture ABI; no arm/finish sent')
        if a.action == 'status':
            report = dict(rom=base, capture=before)
        elif a.action == 'arm':
            if before.get('phase') == 1:
                raise RuntimeError('previous capture still armed; do not replace it')
            armed = request(port, 'write-arm')
            report = dict(rom=base, before=before, capture=armed,
                          expected_address=a.expected_address, expected_value=a.expected_value)
        else:
            arm = json.loads((a.directory / 'arm.json').read_text())
            if before.get('session') != arm['capture']['session'] or before.get('phase') != 1:
                raise RuntimeError('capture reset/changed/not armed; no finish sent')
            result = request(port, 'write-finish')
            report = dict(rom=base, capture=result,
                          expected_address=arm['expected_address'], expected_value=arm['expected_value'])
    # Persist even rejected captures before raising verification errors.
    with output.open('x') as f:
        json.dump(report, f, indent=2); f.write('\n')
    if a.action == 'arm':
        r = report['capture']
        if r.get('error') or r.get('phase') != 1 or r.get('session') != before['session'] + 1:
            raise RuntimeError('arm rejected; inspect saved response')
    elif a.action == 'finish':
        verdict = verify_frame(result, arm['capture']['session'],
                               arm['expected_address'], arm['expected_value'])
        (a.directory / 'verified.json').write_text(json.dumps(verdict, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
