"""Transport framing and evidence preservation without oscilloscope hardware."""
import importlib.util
import json
import hashlib
from pathlib import Path
from unittest.mock import patch
import os
import pty
import tty
import tempfile
import threading
import unittest

spec = importlib.util.spec_from_file_location('scope', Path(__file__).resolve().parents[1] /
                                             'scripts/hantek_scope.py')
hantek = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hantek)


def packet(total=103, uploaded=0, first=True):
    body = f'{total:09d}{uploaded:09d}'.encode()
    body += b'0' * 99 if first else b'\x00\x01\x02\x03'
    return b'#9' + f'{len(body)+11:09d}'.encode() + body


class Tests(unittest.TestCase):
    def transport(self, reply):
        client, server = pty.openpty()
        tty.setraw(client)
        tty.setraw(server)
        scope = hantek.Scope.__new__(hantek.Scope)
        scope.usbtmc = False
        scope.eom = False
        scope.fd = client
        scope.response = bytearray()
        scope.pending = bytearray()
        scope.transcript = []
        scope.timeout = 0.1
        self.addCleanup(scope.close)
        self.addCleanup(os.close, server)
        def respond():
            os.read(server, 1000)
            for chunk in reply:
                os.write(server, chunk)
        thread = threading.Thread(target=respond)
        thread.start()
        self.addCleanup(thread.join)
        return scope

    def test_fragmented_block_and_optional_newline(self):
        raw = packet()
        scope = self.transport([b'\r\n', raw[:1], raw[1:7], raw[7:13], raw[13:]])
        self.assertEqual(scope.block(':WAVeform:DATA:ALL?'), raw)
        metadata = hantek.packet_metadata(raw, True)
        self.assertEqual(metadata['total_bytes'], 103)
        self.assertEqual(metadata['offset_bytes'], 0)

    def test_disabled_channel_sentinel(self):
        scope = self.transport([b'#9000000000'])
        self.assertEqual(scope.block('query?'), b'#9000000000')
        self.assertTrue(hantek.packet_metadata(b'#9000000000', False)
                        ['disabled_channel_sentinel'])

    def test_text_fragmentation(self):
        scope = self.transport([b'HANTEK,', b'DSO2D15\r\n'])
        self.assertEqual(scope.text('*IDN?'), 'HANTEK,DSO2D15')

    def test_usbtmc_starts_read_and_honors_eom_without_newline(self):
        scope = hantek.Scope.__new__(hantek.Scope)
        scope.fd, scope.timeout, scope.usbtmc, scope.eom = 42, 0.1, True, False
        scope.pending, scope.response, scope.transcript = bytearray(), bytearray(), []
        def ioctl(fd, command, data, *unused):
            if command == 0x80015b18:
                data[0] = 1
        with patch.object(hantek.os, 'write', return_value=6), \
             patch.object(hantek.os, 'read', return_value=b'HANTEK,DSO2D15'), \
             patch.object(hantek.fcntl, 'ioctl', side_effect=ioctl) as control, \
             patch.object(hantek.select, 'select', return_value=([], [42], [])) as poll:
            self.assertEqual(scope.text('*IDN?'), 'HANTEK,DSO2D15')
            self.assertEqual(control.call_args_list[0].args[1], 0x40045b0a)
            # Only write readiness is polled; receive initiates a USB request.
            self.assertEqual(poll.call_count, 1)

    def test_premature_eom_preserves_partial_response(self):
        scope = self.transport([b'#9000000016ab'])
        # Simulate real USBTMC delivering a complete, short message.
        def receive(deadline):
            scope.pending.extend(b'#9000000016ab')
            scope.eom = True
        scope.receive = receive
        with self.assertRaises(EOFError):
            scope.block('query?')

    def test_invalid_and_oversized_blocks(self):
        for wire in (b'#0', b'#x', b'#2xx', b'#9999999999'):
            with self.subTest(wire=wire):
                scope = self.transport([wire])
                with self.assertRaises(ValueError):
                    scope.block('query?')

    def test_short_block_has_deadline(self):
        scope = self.transport([b'#9000000016ab'])
        with self.assertRaises(TimeoutError):
            scope.block('query?')

    def test_malformed_vendor_header(self):
        for raw in (b'#9000000000', packet()[:-1], b'#9000000001x'):
            with self.assertRaises(ValueError):
                hantek.packet_metadata(raw, True)

    def test_capture_preserves_packets_failure_and_refuses_overwrite(self):
        class Fake:
            transcript = []
            pending = bytearray()
            response = bytearray()
            def text(self, command):
                return 'fixture'
            def block(self, command):
                return b'not a packet'
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp) / 'evidence'
            with self.assertRaises(ValueError):
                hantek.acquire(Fake(), directory)
            self.assertEqual((directory/'packet-0000.bin').read_bytes(), b'not a packet')
            report = json.loads((directory/'report.json').read_text())
            self.assertFalse(report['complete'])
            self.assertIn('error', report)
            with self.assertRaises(FileExistsError):
                hantek.acquire(Fake(), directory)

    def test_complete_transfer_and_counter_guard(self):
        class Fake:
            transcript = []
            pending = bytearray()
            response = bytearray()
            def __init__(self, values):
                self.values = iter(values)
            def text(self, command):
                return 'fixture'
            def block(self, command):
                return next(self.values)
        with tempfile.TemporaryDirectory() as tmp:
            good = hantek.acquire(Fake([packet(), b'#9000000000', packet(uploaded=99, first=False)]),
                                  Path(tmp)/'good')
            self.assertTrue(good['complete'])
            self.assertFalse(good['timing_qualified'])
            with self.assertRaises(ValueError):
                hantek.acquire(Fake([packet(), packet(first=False)]), Path(tmp)/'stuck')


class DecoderTests(unittest.TestCase):
    def fixture(self, directory):
        config = {'*IDN?': 'fixture,1.1.0(20210416.00)', ':RUNning?': 'ON',
                  ':CHANnel1:DISPlay?': '0', ':CHANnel2:DISPlay?': '1',
                  ':CHANnel2:OFFSet?': '0', ':CHANnel2:INVert?': '0',
                  ':CHANnel2:COUPling?': 'DC', ':CHANnel2:SCALe?': '5',
                  ':ACQuire:TYPE?': 'NORMal', ':ACQuire:SRATe?': '500000',
                  ':ACQuire:POINts?': '4000'}
        # Signed data with negative baseline; 500 samples/period at500kS/s.
        data = (bytes([255])*250 + bytes([25])*250)*8
        header = bytearray(b'0'*99)
        fields = b'4.9e-3194.9e-3194.9e-3184.9e-31801005.000e+05000001'
        header[18:18+len(fields)] = fields
        def block(offset, body):
            return b'#9'+f'{len(body)+29:09d}{4099:09d}{offset:09d}'.encode()+body
        packets = [block(0, header), b'#9000000000', block(99, data)]
        report = {'complete': True, 'settings': config, 'packets': []}
        for index, wire in enumerate(packets):
            name = f'packet-{index:04d}.bin'
            (directory/name).write_bytes(wire)
            report['packets'].append({'file': name,
                'sha256': hashlib.sha256(wire).hexdigest()})
        (directory/'report.json').write_text(json.dumps(report))
        return report

    def test_signed_scale_frequency_and_sentinel(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            self.fixture(directory)
            result, codes, volts = hantek.decode_single(directory)
            self.assertEqual(result['channel'], 2)
            self.assertEqual(result['frequency_hz'], 1000)
            self.assertEqual(result['low_plateau_v'], -0.2)
            self.assertEqual(result['high_plateau_v'], 5)
            self.assertEqual(codes[0], -1)
            self.assertFalse(result['timing_qualified'])
            hantek.analyze(directory, directory/'output')
            with self.assertRaises(FileExistsError):
                hantek.analyze(directory, directory/'output')

    def test_rejects_ambiguous_or_mismatched_metadata(self):
        mutations = [(':RUNning?', 'OFF'), (':CHANnel1:DISPlay?', '1'),
                     (':CHANnel2:OFFSet?', '1'),
                     (':CHANnel2:INVert?', '1'),
                     (':ACQuire:SRATe?', '1000000000'),
                     (':ACQuire:POINts?', '3999'),
                     ('*IDN?', 'different firmware')]
        for key, value in mutations:
            with self.subTest(key=key), tempfile.TemporaryDirectory() as tmp:
                directory = Path(tmp)
                report = self.fixture(directory)
                report['settings'][key] = value
                (directory/'report.json').write_text(json.dumps(report))
                with self.assertRaises(ValueError):
                    hantek.decode_single(directory)

    def test_rejects_tampered_evidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            self.fixture(directory)
            (directory/'packet-0002.bin').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'hash'):
                hantek.decode_single(directory)


class CursorTests(unittest.TestCase):
    class FakeScope:
        def __init__(self):
            self.values = {':CURSor:MODE': 'OFF',
                           ':CURSor:TRACk:SOURcea': 'CHANnel2',
                           ':CURSor:TRACk:SOURceb': 'CHANnel1',
                           ':CURSor:TRACk:AX': '12', ':CURSor:TRACk:BX': '34'}
            self.original = dict(self.values)
            self.transcript = []
            self.running = 'OFF'
            self.fail = False
        def send(self, command):
            key, value = command.split(' ', 1)
            self.values[key] = value
            self.transcript.append({'command': command})
        def text(self, command):
            if command == ':RUNning?':
                return self.running
            if command.endswith(('AXValue?', 'BXValue?')):
                return '0'
            if command.endswith(('AYValue?', 'BYValue?')):
                return 'nan' if self.fail else '5'
            return self.values[command[:-1]]

    def test_stopped_export_restores_cursor_and_preserves_evidence(self):
        scope = self.FakeScope()
        config = {':RUNning?': 'OFF', ':CHANnel1:DISPlay?': '1',
                  ':CHANnel2:DISPlay?': '1'}
        with tempfile.TemporaryDirectory() as tmp, \
             patch.object(hantek, 'settings', return_value=config), \
             patch.object(hantek.time, 'sleep'):
            directory = Path(tmp)/'capture'
            result = hantek.cursor_acquire(scope, directory, 10, 20, 10)
            self.assertTrue(result['complete'])
            self.assertTrue(result['cursor_restored'])
            self.assertEqual(len(result['points']), 2)
            self.assertFalse(result['timing_qualified'])
            self.assertEqual(scope.values, scope.original)
            with self.assertRaises(FileExistsError):
                hantek.cursor_acquire(scope, directory)

    def test_bad_sample_restores_cursor_and_records_failure(self):
        scope = self.FakeScope()
        scope.fail = True
        config = {':RUNning?': 'OFF', ':CHANnel1:DISPlay?': '1',
                  ':CHANnel2:DISPlay?': '1'}
        with tempfile.TemporaryDirectory() as tmp, \
             patch.object(hantek, 'settings', return_value=config), \
             patch.object(hantek.time, 'sleep'):
            directory = Path(tmp)/'capture'
            with self.assertRaisesRegex(ValueError, 'Nonfinite'):
                hantek.cursor_acquire(scope, directory, 10, 20, 10)
            result = json.loads((directory/'report.json').read_text())
            self.assertFalse(result['complete'])
            self.assertTrue(result['cursor_restored'])
            self.assertEqual(scope.values, scope.original)

    def test_running_scope_refused_without_cursor_mutation(self):
        scope = self.FakeScope()
        with tempfile.TemporaryDirectory() as tmp, \
             patch.object(hantek, 'settings', return_value={':RUNning?': 'ON'}):
            with self.assertRaisesRegex(ValueError, 'stopped'):
                hantek.cursor_acquire(scope, Path(tmp)/'capture')
            self.assertEqual(scope.values, scope.original)


if __name__ == '__main__':
    unittest.main()
