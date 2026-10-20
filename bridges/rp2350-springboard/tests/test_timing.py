"""Synthetic logic captures test decoding, boundaries and honest timing limits."""
import configparser
import importlib.util
from pathlib import Path
import tempfile
import unittest
from zipfile import ZipFile

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/analyze_timing.py"
spec = importlib.util.spec_from_file_location("analyze_timing", SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def burst(oe_before=False):
    data = bytearray([0x8F] * 20)
    for word in range(8):
        idle = 0x8F | (word << 4)
        data += bytes([idle] * 10)
        if oe_before:
            data += bytes([idle & ~2] * 2)
            data += bytes([idle & ~3] * 8)
            data += bytes([idle & ~2] * 2)
        else:
            data += bytes([idle & ~1] * 2)
            data += bytes([idle & ~3] * 8)
            data += bytes([idle & ~1] * 2)
        data += bytes([idle] * 10)
    return bytes(data + bytes([0xFF] * 20))


class TimingTests(unittest.TestCase):
    def test_both_orders_and_sample_units(self):
        for early in (False, True):
            report = module.analyze(burst(early), 24_000_000)
            self.assertEqual(report["result"], "MATCH")
            self.assertEqual(report["observed_low_address_sequence"], list(range(0, 16, 2)))
            self.assertEqual(report["complete_cs0_pulses"], 8)
            self.assertFalse(report["timing_qualified"])
            self.assertFalse(report["hardware_rom_response_verified"])
            self.assertAlmostEqual(report["sample_interval_ns"], 41.6666666667)
            first = report["cycles"][0]
            self.assertAlmostEqual(first["cs_width_ns"], (8 if early else 12) * 1e9 / 24_000_000)
            self.assertAlmostEqual(first["oe_minus_cs_fall_ns"], (-2 if early else 2) * 1e9 / 24_000_000)
            self.assertAlmostEqual(first["oe_minus_cs_rise_ns"], (2 if early else -2) * 1e9 / 24_000_000)

    def test_empty_and_clipped_pulses_not_pass(self):
        for data in (bytes([0x8F] * 100), bytes([0] * 100), burst()[:-30]):
            self.assertEqual(module.analyze(data, 24_000_000)["result"], "INCONCLUSIVE")
        with self.assertRaises(ValueError):
            module.analyze(b"", 0)

    def test_forbidden_control_missing_oe_and_unstable_address(self):
        data = bytearray(burst())
        first = module.analyze(data, 24_000_000)["cycles"][0]
        for mask in (4, 8, 128):
            bad = bytearray(data)
            bad[first["start_sample"] + 3] &= ~mask
            self.assertEqual(module.analyze(bad, 24_000_000)["result"], "INCONCLUSIVE")
        bad = bytes(x | 2 for x in data)
        self.assertEqual(module.analyze(bad, 24_000_000)["read_observations"], 0)
        data[first["start_sample"] + 4] ^= 0x10
        self.assertEqual(module.analyze(data, 24_000_000)["result"], "INCONCLUSIVE")

    def test_session_chunks_metadata_and_validation(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test.sr"
            data = burst()
            info = "[device 1]\ncapturefile=logic-1\nunitsize=1\ntotal probes=8\nsamplerate=24 MHz\n"
            info += "".join(f"probe{i + 1}=D{i}\n" for i in range(8))
            def write(metadata=info, chunk=2):
                with ZipFile(path, "w") as z:
                    z.writestr("version", "2")
                    z.writestr("metadata", metadata)
                    z.writestr("logic-1-1", data[:100])
                    z.writestr(f"logic-1-{chunk}", data[100:])
            write()
            loaded, rate = module.load_session(path)
            self.assertEqual(loaded, data)
            self.assertEqual(rate, 24_000_000)
            for change in (info.replace("D0", "CS0"), info.replace("unitsize=1", "unitsize=2"),
                           info.replace("24 MHz", "0 Hz")):
                write(change)
                with self.assertRaises(ValueError):
                    module.load_session(path)
            write(chunk=3)
            with self.assertRaises(ValueError):
                module.load_session(path)


if __name__ == "__main__":
    unittest.main()
