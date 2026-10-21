#!/usr/bin/env python3
"""Conservative Linux USBTMC tooling for Hantek DSO2000 scopes (stdlib only).

The vendor waveform protocol is not a Rigol preamble. Preserve its packets;
voltage conversion and channel ordering remain unqualified until measured.
"""
import argparse
import csv
import math
import re
import statistics
import errno
import fcntl
import struct
import hashlib
import json
import os
from pathlib import Path
import select
import time
from datetime import datetime, timezone

MAX_PACKET = 16 * 1024 * 1024
MANUAL = ('https://hantek.com/uploadpic/hantek/files/20211231/'
          'DSO2000%20Series%C2%A0SCPI%C2%A0Programmers%C2%A0Manual.pdf')


class Scope:
    def __init__(self, device, timeout=5):
        self.fd = os.open(device, os.O_RDWR)
        try:
            fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except Exception:
            os.close(self.fd)
            raise
        self.usbtmc = True
        self.eom = False
        self.timeout = timeout
        self.response = bytearray()
        self.pending = bytearray()
        self.transcript = []

    def close(self):
        os.close(self.fd)

    def send(self, command):
        if '\n' in command or '\r' in command:
            raise ValueError('One command per transaction required')
        self.response = bytearray()
        self.eom = False
        data = (command + '\n').encode('ascii')
        deadline = time.monotonic() + self.timeout
        while data:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([], [self.fd], [], remaining)[1]:
                raise TimeoutError('USBTMC write timed out')
            try:
                count = os.write(self.fd, data)
            except BlockingIOError:
                continue
            if not count:
                raise OSError('USBTMC write returned zero')
            data = data[count:]
        self.transcript.append({'command': command})

    def receive(self, deadline):
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError('USBTMC response timed out')
            if self.usbtmc:
                # read() initiates DEV_DEP_MSG_IN. poll() alone never starts it.
                # Linux UAPI _IOW(91,10,u32), _IOR(91,24,u8), x86/ARM encoding.
                fcntl.ioctl(self.fd, 0x40045b0a,
                            struct.pack('I', max(100, int(remaining * 1000))))
            elif not select.select([self.fd], [], [], remaining)[0]:
                raise TimeoutError('Response timed out')
            try:
                data = os.read(self.fd, 65536)
            except BlockingIOError:
                continue
            except OSError as error:
                if error.errno == errno.ETIMEDOUT:
                    raise TimeoutError('USBTMC response timed out') from error
                raise
            if not data:
                raise EOFError('Scope disconnected or returned an empty response')
            if self.usbtmc:
                attributes = bytearray(1)
                fcntl.ioctl(self.fd, 0x80015b18, attributes, True)
                self.eom = bool(attributes[0] & 1)
            self.pending.extend(data)
            self.response.extend(data)
            return

    def exact(self, count, deadline):
        while len(self.pending) < count:
            if self.eom:
                raise EOFError('USBTMC message ended before declared packet length')
            self.receive(deadline)
        result = bytes(self.pending[:count])
        del self.pending[:count]
        return result

    def text(self, command):
        self.send(command)
        deadline = time.monotonic() + self.timeout
        # Actual firmware replies have no LF; USBTMC EOM terminates them.
        self.eom = False
        while b'\n' not in self.pending and not self.eom:
            if len(self.pending) > 65536:
                raise ValueError('Oversized text response')
            self.receive(deadline)
        end = self.pending.index(b'\n') + 1 if b'\n' in self.pending else len(self.pending)
        response = bytes(self.pending[:end]).decode('ascii').strip()
        del self.pending[:end]
        self.transcript[-1]['response'] = response
        return response

    def block(self, command):
        self.send(command)
        deadline = time.monotonic() + self.timeout
        # Discard only a previous block's optional line terminator.
        while True:
            first = self.exact(1, deadline)
            if first not in (b'\n', b'\r'):
                break
        if first != b'#':
            raise ValueError(f'Expected binary block, got {first!r}')
        digit = self.exact(1, deadline)
        if digit not in b'123456789':
            raise ValueError('Indefinite or invalid block length')
        length_text = self.exact(int(digit), deadline)
        length = decimal(length_text)
        # Vendor length includes '#9' and the nine length digits. It is
        # deliberately NOT IEEE488.2 payload-length framing.
        header = b'#' + digit + length_text
        # Disabled channels emit the literal zero-length vendor sentinel.
        if length == 0 and header == b'#9000000000':
            return header
        if length < len(header):
            raise ValueError('Packet length is smaller than its length header')
        if length > MAX_PACKET:
            raise ValueError('Waveform packet exceeds bounded allocation')
        return header + self.exact(length - len(header), deadline)


def decimal(data):
    if not data or not data.isdigit():
        raise ValueError(f'Invalid decimal field: {data!r}')
    return int(data)


def packet_metadata(packet, first):
    """Decode only documented byte ranges; retain ambiguous fields verbatim."""
    if packet == b'#9000000000' and not first:
        return {'packet_bytes': 11, 'disabled_channel_sentinel': True}
    if len(packet) < 29 or packet[:2] != b'#9':
        raise ValueError('Expected Hantek #9 packet with transfer counters')
    size = decimal(packet[2:11])
    if len(packet) != size:
        raise ValueError('Packet length disagrees with Hantek total packet length')
    result = {'packet_bytes': size, 'transfer_payload_bytes': size - 29,
              'total_bytes': decimal(packet[11:20]),
              'offset_bytes': decimal(packet[20:29])}
    if first and len(packet) != 128:
        raise ValueError('Expected 128-byte initial Hantek waveform header')
    if first:
        if len(packet) < 128:
            raise ValueError('Truncated Hantek initial waveform header')
        result.update(run_status_raw=packet[29:30].hex(),
                      trigger_status_raw=packet[30:31].hex(),
                      header_hex=packet[29:128].hex(),
                      header_layout_qualified=False)
        # Firmware1.1.0 expands voltage fields beyond the manual's widths;
        # downstream offsets differ. Never label those slices as calibrated
        # metadata. Query settings separately and preserve the entire header.

    return result


def settings(scope):
    commands = ['*IDN?', ':RUNning?', ':TIMebase:SCALe?', ':TIMebase:POSition?',
                ':ACQuire:SRATe?', ':ACQuire:POINts?', ':ACQuire:TYPE?',
                ':TRIGger:MODE?', ':TRIGger:SWEep?', ':TRIGger:STATus?',
                ':TRIGger:EDGe:SOURce?', ':TRIGger:EDGe:SLOPe?',
                ':TRIGger:EDGe:LEVel?']
    for channel in (1, 2):
        commands.extend(f':CHANnel{channel}:{field}?' for field in
                        ('DISPlay', 'PROBe', 'SCALe', 'OFFSet', 'COUPling',
                         'BWLimit', 'INVert'))
    return {command: scope.text(command) for command in commands}


def acquire(scope, directory, max_packets=4096):
    directory.mkdir(parents=True, exist_ok=False)
    report = {'started_utc': datetime.now(timezone.utc).isoformat(),
              'manual': MANUAL, 'packets': [], 'complete': False,
              'timing_qualified': False,
              'decoding': 'Raw vendor packets; voltage/channel layout unqualified'}
    try:
        report['settings'] = settings(scope)
        # No run/stop/reset writes. Operator freezes the intended acquisition
        # using the front panel before invoking this command.
        expected_total = None
        expected_offset = 0
        empty_slots = 0
        for index in range(max_packets):
            packet = scope.block(':WAVeform:DATA:ALL?')
            name = f'packet-{index:04d}.bin'
            (directory / name).write_bytes(packet)
            metadata = packet_metadata(packet, first=index == 0)
            metadata.update(file=name, sha256=hashlib.sha256(packet).hexdigest())
            report['packets'].append(metadata)
            if metadata.get('disabled_channel_sentinel'):
                empty_slots += 1
                if empty_slots > 4:
                    raise ValueError('Too many disabled-channel sentinels')
                continue
            total, offset = metadata['total_bytes'], metadata['offset_bytes']
            if expected_total is None:
                expected_total = total
            end = offset + metadata['transfer_payload_bytes']
            if not total or total != expected_total or end > total:
                raise ValueError('Invalid or changing waveform transfer counters')
            if offset != expected_offset or end <= offset:
                raise ValueError('Noncontiguous waveform transfer offset')
            expected_offset = end
            if end == total:
                report['complete'] = True
                break
        if not report['complete']:
            raise ValueError('Waveform transfer exceeded packet limit')
    except Exception as error:
        report['error'] = f'{type(error).__name__}: {error}'
        if scope.response:
            (directory / 'partial-response.bin').write_bytes(scope.response)
        raise
    finally:
        report['transcript'] = scope.transcript
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        (directory / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def configure(scope, profile):
    """Set two-channel presets; SINGLE can arm acquisition on firmware1.1.0.

    Does not force a trigger. Callers must verify actual running/trigger state.
    """
    before = settings(scope)
    scale = '2' if profile == 'calibration-edge' else '1'
    commands = [':TIMebase:MODE MAIN', ':TIMebase:WINDow:ENABle OFF',
                ':ACQuire:TYPE NORMal', ':ACQuire:POINts 4000']
    for channel in (1, 2):
        commands.extend(f':CHANnel{channel}:{setting}' for setting in
                        ('DISPlay ON', 'PROBe 10', 'COUPling DC',
                         'BWLimit OFF', 'INVert OFF', 'SCALe '+scale, 'OFFSet 0'))
    commands += [':TIMebase:SCALe ' + ('0.0005' if profile == 'calibration' else '0.0000001'),
                 ':TIMebase:POSition 0', ':TRIGger:MODE EDGE',
                 ':TRIGger:EDGe:SOURce CHANnel1',
                 ':TRIGger:EDGe:SLOPe ' + ('FALLing' if profile == 'bus' else 'RISIng'),
                 ':TRIGger:EDGe:LEVel 1.65',
                 ':TRIGger:SWEep ' + ('AUTO' if profile == 'calibration' else 'SINGle')]
    for command in commands:
        scope.send(command)
        # Allow firmware's command processor to apply each setting.
        time.sleep(0.05)
    after = settings(scope)
    warnings = []
    if profile != 'calibration' and after[':TRIGger:SWEep?'].upper() != 'SINGLE':
        warnings.append('Remote SINGLE setting did not apply; use front-panel SINGLE button')
    return {'profile': profile, 'before': before, 'after': after,
            'commands': commands, 'warnings': warnings,
            'note': 'SINGLE may arm acquisition; verify running/trigger state before the read cue.'}


def decode_single(directory):
    """Decode validated zero-offset, normal single-channel firmware1.1.0 data.

    The 25 signed ADC counts/division mapping is an empirical probe-comp check,
    not a traceable calibration. Time zero is sample0, never a guessed trigger.
    """
    report = json.loads((directory / 'report.json').read_text())
    if not report['complete']:
        raise ValueError('Refusing incomplete waveform transfer')
    config = report['settings']
    if config.get(':RUNning?') != 'ON':
        raise ValueError('Frozen-channel selection does not establish sample source; stopped decoding unvalidated')
    if '1.1.0(20210416.00)' not in config['*IDN?']:
        raise ValueError('Unvalidated scope firmware/sample encoding')
    channels = [c for c in (1, 2) if config[f':CHANnel{c}:DISPlay?'] == '1']
    if len(channels) != 1:
        raise ValueError('Single-channel decoding only: dual-channel USB data duplicates CH1')
    channel = channels[0]
    prefix = f':CHANnel{channel}:'
    if (float(config[prefix+'OFFSet?']) != 0 or
            config[prefix+'INVert?'] != '0' or
            config[prefix+'COUPling?'] != 'DC' or
            config[':ACQuire:TYPE?'] != 'NORMal'):
        raise ValueError('Only zero-offset, uninverted normal DC acquisition validated')
    scale = float(config[prefix+'SCALe?'])
    rate = float(config[':ACQuire:SRATe?'])
    if not all(math.isfinite(x) and x > 0 for x in (scale, rate)):
        raise ValueError('Invalid sample rate or voltage scale')
    data = bytearray()
    offset = 0
    for index, metadata in enumerate(report['packets']):
        name = metadata['file']
        if Path(name).name != name:
            raise ValueError('Unsafe packet filename')
        packet = (directory / name).read_bytes()
        if hashlib.sha256(packet).hexdigest() != metadata['sha256']:
            raise ValueError('Waveform packet hash mismatch')
        decoded = packet_metadata(packet, first=index == 0)
        if decoded.get('disabled_channel_sentinel'):
            continue
        if decoded['offset_bytes'] != offset:
            raise ValueError('Noncontiguous saved transfer')
        offset += len(packet) - 29
        if index == 0:
            # Locate observed fields structurally: old firmware's invalid
            # voltage fields expand beyond the widths in the PDF manual.
            match = re.search(rb'([01]{4})([0-9]\.[0-9]{3}e[+-][0-9]{2})([0-9]{6})',
                              packet[47:116])
            if not match or float(match[2]) != rate or int(match[3]) != 1:
                raise ValueError('Header/query sample rate mismatch or decimation')
            mask = ''.join('1' if c == channel else '0' for c in range(1, 5))
            if match[1].decode() != mask:
                raise ValueError('Header/query channel mask mismatch')
        else:
            data.extend(packet[29:])
    if offset != decoded.get('total_bytes') or len(data) != int(config[':ACQuire:POINts?']):
        raise ValueError('Sample count or final transfer size mismatch')
    codes = [value if value < 128 else value - 256 for value in data]
    volts = [value * scale / 25 for value in codes]
    threshold = (min(volts) + max(volts)) / 2
    low = [value for value in volts if value < threshold]
    high = [value for value in volts if value >= threshold]
    rises = []
    for index in range(1, len(volts)):
        before, after = volts[index-1:index+1]
        if before < threshold <= after:
            rises.append((index-1 + (threshold-before)/(after-before)) / rate)
    periods = [b-a for a, b in zip(rises, rises[1:])]
    summary = {'channel': channel, 'samples': len(codes), 'sample_rate_hz': rate,
               'sample_spacing_s': 1/rate, 'volts_per_division': scale,
               'conversion': 'signed int8 * queried V/div / 25; zero offset only',
               'time_origin': 'first downloaded sample; not trigger-relative',
               'low_plateau_v': statistics.median(low) if low else None,
               'high_plateau_v': statistics.median(high) if high else None,
               'rail_samples': sum(x in (-128, 127) for x in codes),
               'rising_edges': len(rises),
               'frequency_hz': 1/statistics.median(periods) if periods else None,
               'timing_qualified': False,
               'reference': 'Empirical probe compensation check, not traceable calibration'}
    return summary, codes, volts


def analyze(directory, output):
    summary, codes, volts = decode_single(directory)
    output.mkdir(parents=True, exist_ok=False)
    with (output/'samples.csv').open('w', newline='') as handle:
        writer = csv.writer(handle)
        writer.writerow(['sample', 'seconds_from_first_sample', 'signed_code', 'volts'])
        for index, (code, voltage) in enumerate(zip(codes, volts)):
            writer.writerow([index, index/summary['sample_rate_hz'], code, voltage])
    (output/'analysis.json').write_text(json.dumps(summary, indent=2)+'\n')
    return summary


def cursor_acquire(scope, directory, start=0, end=770, step=4):
    """Export both stopped screen traces through documented tracking cursors.

    Pixel coordinates and scope-reported times remain raw: firmware reverses
    the time sign, and display interpolation/quantization are not ADC resolution.
    """
    if not (0 <= start <= end <= 770 and 1 <= step <= 770):
        raise ValueError('Cursor range must be within0..770 with positive step')
    directory.mkdir(parents=True, exist_ok=False)
    report = {'complete': False, 'method': 'stopped tracking cursors',
              'timing_qualified': False, 'points': [],
              'started_utc': datetime.now(timezone.utc).isoformat(),
              'time_axis': 'Scope-reported cursor time; sign/origin unqualified'}
    cursor_keys = (':CURSor:MODE?', ':CURSor:TRACk:SOURcea?',
                   ':CURSor:TRACk:SOURceb?', ':CURSor:TRACk:AX?',
                   ':CURSor:TRACk:BX?')
    saved = None
    try:
        report['settings'] = settings(scope)
        config = report['settings']
        if config[':RUNning?'] != 'OFF':
            raise ValueError('Cursor export requires stopped acquisition')
        if any(config[f':CHANnel{c}:DISPlay?'] != '1' for c in (1, 2)):
            raise ValueError('Both captured channels must remain enabled')
        saved = {key: scope.text(key) for key in cursor_keys}
        report['cursor_before'] = saved
        for command in (':CURSor:MODE TRACk',
                        ':CURSor:TRACk:SOURcea CHANnel1',
                        ':CURSor:TRACk:SOURceb CHANnel2'):
            scope.send(command)
            time.sleep(0.05)
        sources = {key: scope.text(key) for key in cursor_keys[:3]}
        report['cursor_selected'] = sources
        if (sources[cursor_keys[0]].upper() != 'TRACK' or
                sources[cursor_keys[1]].upper() != 'CHANNEL1' or
                sources[cursor_keys[2]].upper() != 'CHANNEL2'):
            raise ValueError('Tracking cursor source readback mismatch')
        for pixel in range(start, end+1, step):
            if scope.text(':RUNning?') != 'OFF':
                raise ValueError('Acquisition resumed during cursor export')
            scope.send(f':CURSor:TRACk:AX {pixel}')
            scope.send(f':CURSor:TRACk:BX {pixel}')
            time.sleep(0.02)
            row = {'pixel': pixel}
            for field, command in (('ch1_time_s', 'AXValue'), ('ch1_v', 'AYValue'),
                                   ('ch2_time_s', 'BXValue'), ('ch2_v', 'BYValue')):
                row[field] = float(scope.text(f':CURSor:TRACk:{command}?'))
            if not all(math.isfinite(value) for value in row.values()):
                raise ValueError('Nonfinite cursor measurement')
            if row['ch1_time_s'] != row['ch2_time_s']:
                raise ValueError('Cursor positions have different time coordinates')
            report['points'].append(row)
        report['settings_after'] = settings(scope)
        if report['settings_after'] != config:
            raise ValueError('Acquisition/channel settings changed during cursor export')
        report['complete'] = True
    except Exception as error:
        report['error'] = f'{type(error).__name__}: {error}'
        raise
    finally:
        try:
            if saved:
                for key in cursor_keys[1:]:
                    scope.send(key[:-1] + ' ' + saved[key])
                scope.send(':CURSor:MODE ' + saved[cursor_keys[0]])
                report['cursor_after'] = {key: scope.text(key) for key in cursor_keys}
                report['cursor_restored'] = report['cursor_after'] == saved
                if not report['cursor_restored']:
                    report['complete'] = False
        except Exception as error:
            report['restore_error'] = f'{type(error).__name__}: {error}'
            report['complete'] = False
        report['transcript'] = scope.transcript
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        (directory/'report.json').write_text(json.dumps(report, indent=2)+'\n')
        with (directory/'cursor.csv').open('w', newline='') as handle:
            fields = ['pixel', 'ch1_time_s', 'ch1_v', 'ch2_time_s', 'ch2_v']
            writer = csv.DictWriter(handle, fieldnames=fields)
            writer.writeheader()
            writer.writerows(report['points'])
    if not report['complete']:
        raise ValueError('Cursor export incomplete or cursor restoration failed')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', default='/dev/usbtmc3')
    parser.add_argument('--timeout', type=float, default=5)
    sub = parser.add_subparsers(dest='action', required=True)
    sub.add_parser('identify')
    sub.add_parser('settings')
    setup = sub.add_parser('setup', help='Explicit two-channel acquisition preset')
    setup.add_argument('profile', choices=('calibration', 'calibration-edge', 'bus'))
    download = sub.add_parser('download', help='Download manually stopped waveform')
    download.add_argument('directory', type=Path)
    decoder = sub.add_parser('analyze', help='Decode validated single-channel capture')
    decoder.add_argument('directory', type=Path)
    decoder.add_argument('output', type=Path)
    cursors = sub.add_parser('cursor-download', help='Export both stopped screen traces')
    cursors.add_argument('directory', type=Path)
    cursors.add_argument('--start', type=int, default=0)
    cursors.add_argument('--end', type=int, default=770)
    cursors.add_argument('--step', type=int, default=4)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    scope = None
    try:
        if args.action == 'analyze':
            print(json.dumps(analyze(args.directory, args.output), indent=2))
            return
        scope = Scope(args.device, args.timeout)
        if args.action == 'identify':
            print(scope.text('*IDN?'))
        elif args.action == 'settings':
            print(json.dumps(settings(scope), indent=2))
        elif args.action == 'setup':
            print(json.dumps(configure(scope, args.profile), indent=2))
        elif args.action == 'cursor-download':
            report = cursor_acquire(scope, args.directory, args.start, args.end, args.step)
            print(json.dumps({k: v for k, v in report.items()
                              if k not in ('points', 'transcript')}, indent=2))
        else:
            print(json.dumps(acquire(scope, args.directory), indent=2))
    except (OSError, ValueError, EOFError, TimeoutError) as error:
        parser.exit(1, f'{type(error).__name__}: {error}\n')
    finally:
        if scope:
            scope.close()


if __name__ == '__main__':
    main()
