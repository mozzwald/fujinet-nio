import struct,unittest,zlib
from decode import decode_record
class Records(unittest.TestCase):
 def record(self,**kw):
  keys='magic version sequence mode error card generation cs0 cs_size cs1 access_ns ram_pc requested completed cs0_reads cs1_reads value mismatches first_expected first_actual ram_proof ticks seconds'.split()
  values=[0x46444632,1,1,1,0,1,5,0x28000000,0x1000000,0x29000000,330,0x50000,1,1,0,1,0xbeef,0,0,0,1,0,0];r=dict(zip(keys,values));r.update(kw);raw=struct.pack('>23I',*(r[k] for k in keys));return raw+struct.pack('>I',zlib.crc32(raw))
 def test_pass(self):self.assertTrue(decode_record(self.record())['pass'])
 def test_corruption(self):
  bad=bytearray(self.record());bad[30]^=1
  with self.assertRaisesRegex(ValueError,'CRC'):decode_record(bad)
 def test_partial_not_pass(self):self.assertFalse(decode_record(self.record(completed=0))['pass'])
 def test_stale_value_not_pass(self):self.assertFalse(decode_record(self.record(value=0xfeed))['pass'])
 def test_bad_mapping_not_pass(self):self.assertFalse(decode_record(self.record(cs1=0x28000000))['pass'])
 def test_rom_pair(self):self.assertTrue(decode_record(self.record(mode=4,requested=4,completed=4,cs0_reads=8,cs1_reads=0,value=0xfeedbeef))['pass'])
 def test_failure(self):self.assertFalse(decode_record(self.record(error=5))['pass'])
if __name__=='__main__':unittest.main()
