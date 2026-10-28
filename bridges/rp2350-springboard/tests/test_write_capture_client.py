import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('wc', Path(__file__).resolve().parents[1] / 'scripts/write_capture.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)


class Capture(unittest.TestCase):
    def good(self):
        return dict(capture_abi=1, session=7, phase=2, result=0, qualified=1,
                    error='', pulses=1, low=5 << 16, low_end=15,
                    high=(1 << 29) | (0x1234 << 4) | 5, high_end=15, outcome=0,
                    low_depth=2, high_depth=2, end_depth=1,
                    reset_edge=0, cs0_edge=0, oe_edge=0, fifo_fault=0,
                    address=0, value=0x1234)

    def test_exact(self):
        self.assertEqual(m.verify_frame(self.good(), 7, 0, 0x1234)['result'], 'PASS')

    def test_faults(self):
        for k in ('reset_edge', 'cs0_edge', 'oe_edge', 'fifo_fault', 'outcome', 'result'):
            r = self.good(); r[k] = 1
            with self.subTest(k=k), self.assertRaises(ValueError):
                m.verify_frame(r, 7, 0, 0x1234)

    def test_counts(self):
        for k, v in [('pulses', 2), ('low_depth', 1), ('high_depth', 3), ('end_depth', 0)]:
            r = self.good(); r[k] = v
            with self.subTest(k=k), self.assertRaises(ValueError):
                m.verify_frame(r, 7, 0, 0x1234)

    def test_late_and_cancel(self):
        for k, v in [('low_end', 7), ('high_end', 0), ('high', 0x12345), ('phase', 1)]:
            r = self.good(); r[k] = v
            with self.subTest(k=k), self.assertRaises(ValueError):
                m.verify_frame(r, 7, 0, 0x1234)

    def test_stale_session(self):
        with self.assertRaises(ValueError): m.verify_frame(self.good(), 8, 0, 0x1234)

    def test_wrong_value(self):
        with self.assertRaises(ValueError): m.verify_frame(self.good(), 7, 0, 0xa55a)

    def test_forged_fields(self):
        r = self.good(); r['address'] = 2
        with self.assertRaises(ValueError): m.verify_frame(r, 7, 2, 0x1234)

    def test_incomplete(self):
        r = self.good(); del r['high']
        with self.assertRaises(ValueError): m.verify_frame(r, 7, 0, 0x1234)


if __name__ == '__main__': unittest.main()
