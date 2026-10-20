"""Regression: inherited EOF must not silently stop the sigrok subprocess."""
import argparse
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import capture_timing
from test_timing import burst


class CaptureTests(unittest.TestCase):
    def test_stdin_is_held_open_and_session_analyzed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            fake = root / "sigrok-cli"
            fake.write_text('''#!/usr/bin/env python3
import os,select,sys,shutil
if '--version' in sys.argv:
 print('synthetic test sigrok');sys.exit(0)
# Old inherited /dev/null or closed stdin would signal ready/EOF and stop.
if select.select([0],[],[],0)[0]:sys.exit(42)
shutil.copyfile(os.environ['FAKE_SR'],sys.argv[sys.argv.index('--output-file')+1])
''')
            fake.chmod(0o755)
            fixture = root / "fixture.sr"
            data = burst() + bytes([0xff] * (1000 - len(burst())))
            metadata = ("[device 1]\ncapturefile=logic-1\nunitsize=1\n"
                        "total probes=8\nsamplerate=12 MHz\n" +
                        "".join(f"probe{i+1}=D{i}\n" for i in range(8)))
            with ZipFile(fixture, "w") as archive:
                archive.writestr("version", "2")
                archive.writestr("metadata", metadata)
                archive.writestr("logic-1-1", data)
            env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'],
                       FAKE_SR=str(fixture))
            # Prove the fake models the actual EOF failure before testing fix.
            baseline = subprocess.run([str(fake), '--output-file', str(root/'bad.sr')],
                                      stdin=subprocess.DEVNULL, env=env)
            self.assertEqual(baseline.returncode, 42)
            args = argparse.Namespace(directory=root/'run', samples=1000,
                                      rate=12000000, timeout=3, driver='fake')
            with patch.dict(os.environ, env), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(capture_timing.capture(args), 0)
            report = json.loads((args.directory/'timing.json').read_text())
            self.assertEqual(report['result'], 'MATCH')
            self.assertFalse(report['timing_qualified'])
            # Evidence directory must never be overwritten by another run.
            with self.assertRaises(FileExistsError):
                capture_timing.capture(args)


if __name__ == '__main__':
    unittest.main()
