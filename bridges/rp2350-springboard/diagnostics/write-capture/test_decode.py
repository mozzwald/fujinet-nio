import struct
import unittest
import zlib
from decode import decode_record, pdb_records


class Journal(unittest.TestCase):
    def record(self, **changes):
        values = [0x46574533, 1, 1, 1, 0, 1, 8, 0x28000000, 0x1000000, 0x29000000,
                  330, 0x63000, 1, 1, 0, 1, 0x1234, 0, 0, 0, 1, 0, 3875946827]
        names = ('magic version sequence mode error card generation cs0 cs_size cs1 access_ns '
                 'ram_pc requested completed cs0_reads writes_issued intended_value reserved '
                 'offset reserved2 ram_proof ticks seconds').split()
        for k, v in changes.items(): values[names.index(k)] = v
        raw = struct.pack('>23I', *values)
        return raw + struct.pack('>I', zlib.crc32(raw))

    def test_issued_not_accepted(self):
        r = decode_record(self.record()); self.assertTrue(r['issued']); self.assertFalse(r['rp_capture_verified'])

    def test_second_pattern(self):
        self.assertTrue(decode_record(self.record(mode=2, intended_value=0xa55a, offset=2))['issued'])

    def test_bus_error(self): self.assertFalse(decode_record(self.record(error=5, completed=0, writes_issued=0))['issued'])

    def test_crc(self):
        r = bytearray(self.record()); r[60] ^= 1
        with self.assertRaises(ValueError): decode_record(r)

    def test_invalid_count_mapping(self):
        for fields in (dict(completed=2), dict(cs1=0x2a000000), dict(cs0_reads=1), dict(offset=2), dict(ram_proof=0)):
            with self.subTest(fields=fields): self.assertFalse(decode_record(self.record(**fields))['issued'])

    def test_length(self):
        with self.assertRaises(ValueError): decode_record(self.record()[:-1])

    def test_database(self):
        with self.assertRaises(ValueError): pdb_records(bytes(200))


if __name__ == '__main__': unittest.main()
