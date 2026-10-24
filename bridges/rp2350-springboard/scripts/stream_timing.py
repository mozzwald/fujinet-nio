#!/usr/bin/env python3
"""Bounded passive capture: detect CS0 falling in a continuous eight-bit stream.

No hardware trigger acknowledgement is claimed. An initial stream-ready message
requires actual samples. Retain one sample of prehistory and the requested
post-edge samples; never store an unlimited manual-trigger wait to disk.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import time
from zipfile import ZipFile, ZIP_DEFLATED


class Window:
    def __init__(self, count, bit=0):
        if count < 2 or not 0 <= bit < 8:
            raise ValueError('invalid capture window')
        self.count = count
        self.table = bytes(bool(v & (1 << bit)) for v in range(256))
        self.previous = b''
        self.data = bytearray()
        self.triggered = False

    def feed(self, chunk):
        if not chunk:
            return
        if not self.triggered:
            combined = self.previous + chunk
            edge = combined.translate(self.table).find(b'\x01\x00')
            self.previous = chunk[-1:]
            if edge < 0:
                return
            self.triggered = True
            chunk = combined[edge:]
        self.data.extend(chunk[:self.count-len(self.data)])

    @property
    def complete(self):
        return len(self.data) == self.count


def capture(args):
    args.directory.mkdir(parents=True, exist_ok=False)
    command = ['sigrok-cli', '--driver', args.driver, '--continuous',
               '--config', f'samplerate={args.rate}', '--channels',
               ','.join(f'D{i}' for i in range(8)), '--output-format', 'binary',
               '--loglevel', '4']
    meta = dict(started_utc=datetime.now(timezone.utc).isoformat(), command=command,
                rate_requested_hz=args.rate, samples_requested=args.samples,
                trigger=f'D{args.bit} falling in host sample stream',
                trigger_index=1, hardware_arm_acknowledgement=False,
                transport_completeness_verified=False, received_bytes=0)
    window = Window(args.samples, args.bit)
    process = None
    try:
        with (args.directory/'sigrok.stderr.log').open('wb') as log:
            process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=log)
            end = time.monotonic()+args.timeout
            while not window.complete:
                if time.monotonic() >= end:
                    raise TimeoutError('stream capture timed out')
                if not select.select([process.stdout], [], [], 1)[0]:
                    if process.poll() is not None:
                        raise RuntimeError('analyzer exited before complete capture')
                    continue
                chunk = os.read(process.stdout.fileno(), 65536)
                if not chunk:
                    raise RuntimeError('analyzer stream ended before complete capture')
                first = not meta['received_bytes']
                meta['received_bytes'] += len(chunk)
                window.feed(chunk)
                if first:
                    print('STREAM RECEIVING SAMPLES: waiting for falling edge; no hardware trigger acknowledgement.', flush=True)
                if window.triggered and not meta.get('edge_seen'):
                    meta['edge_seen'] = True
                    print('Falling edge found; collecting bounded post-edge window.', flush=True)
    except Exception as error:
        meta['error'] = str(error)
        raise
    finally:
        if process:
            if process.poll() is None:
                process.send_signal(signal.SIGINT)
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()
            meta['sigrok_returncode'] = process.returncode
            process.stdin.close(); process.stdout.close()
        meta.update(ended_utc=datetime.now(timezone.utc).isoformat(),
                    samples_saved=len(window.data), window_complete=window.complete)
        (args.directory/'acquisition.json').write_text(json.dumps(meta, indent=2)+'\n')
        if window.data:
            (args.directory/'capture.bin').write_bytes(window.data)
    # Native stream bytes are retained; SR container is an explicit host wrapper.
    metadata = ('[device 1]\ncapturefile=logic-1\nunitsize=1\n'
                f'total probes=8\nsamplerate={args.rate} Hz\n' +
                ''.join(f'probe{i+1}=D{i}\n' for i in range(8)))
    with ZipFile(args.directory/'capture.sr', 'w', ZIP_DEFLATED) as archive:
        archive.writestr('version', '2')
        archive.writestr('metadata', metadata)
        archive.writestr('logic-1-1', window.data)
    meta['payload_sha256'] = hashlib.sha256(window.data).hexdigest()
    meta['session_container'] = 'host wrapped raw binary; rate requested, confirm driver log'
    (args.directory/'acquisition.json').write_text(json.dumps(meta, indent=2)+'\n')
    print(f'Saved {len(window.data)} bounded samples; analyze before qualification.', flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    p.add_argument('--driver', required=True)
    p.add_argument('--rate', type=int, default=12000000)
    p.add_argument('--samples', type=int, default=1000000)
    p.add_argument('--timeout', type=float, default=600)
    p.add_argument('--bit', type=int, default=0)
    a = p.parse_args()
    if not 1000 <= a.samples <= 32000000 or a.rate <= 0 or not 1 <= a.timeout <= 600:
        p.error('invalid bounded capture settings')
    capture(a)
