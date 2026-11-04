#!/usr/bin/env python3
"""PC uploader -> actual native C framing/dispatcher/store, with fake flash."""
import ctypes as C,importlib.util,os,struct,subprocess,tempfile,unittest,zlib
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('rom_usb',ROOT/'scripts/rom_usb.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Transport:
 def __init__(self,lib):self.lib=lib;self.calls=0
 def exchange(self,frame):
  self.calls+=1;buf=C.create_string_buffer(528);n=self.lib.rom_test_exchange(frame,len(frame),buf);return buf.raw[:n]
class Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory();lib=Path(cls.tmp.name)/'rom.so'
  subprocess.run(['cc','-std=c11','-shared','-fPIC','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'src'),str(ROOT/'tests/rom_usb_fixture.c'),str(ROOT/'src/rom_store.c'),str(ROOT/'src/rom_wire.c'),'-o',str(lib)],check=True)
  cls.lib=C.CDLL(str(lib));cls.lib.rom_test_exchange.argtypes=[C.c_char_p,C.c_size_t,C.c_void_p];cls.lib.rom_test_exchange.restype=C.c_size_t;cls.lib.rom_test_corrupt.argtypes=[C.c_uint32];cls.lib.rom_test_metadata.argtypes=[C.c_uint32,C.c_uint32]
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def setUp(self):self.lib.rom_test_reset(1);self.t=Transport(self.lib);self.c=m.Client(self.t);self.image=bytearray((i*7)&255 for i in range(514));self.image[8:12]=bytes.fromhex('feedbeef');self.image=bytes(self.image)
 def maintenance(self):self.lib.rom_test_bus(1);self.c.call(m.QUIESCE)
 def test_roundtrip_activation_reboot_rollback(self):
  self.assertEqual(self.c.status()['served_source'],2);self.maintenance();r=self.c.upload(0,7,self.image);self.assertFalse(r['activated']);self.assertEqual(self.c.status()['selected_slot'],2)
  self.c.call(m.ACTIVATE,struct.pack('<I',0));s=self.c.status();self.assertEqual(s['served_source'],0);self.assertFalse(s['active']);self.assertTrue(s['maintenance'])
  other=self.image[:-2];self.c.upload(1,8,other);self.c.call(m.ACTIVATE,struct.pack('<I',1));self.lib.rom_test_reset(0);self.assertEqual(self.c.status()['served_crc32'],zlib.crc32(other));self.maintenance();self.c.call(m.ROLLBACK);self.assertEqual(self.c.status()['served_crc32'],zlib.crc32(self.image));self.c.call(m.RECOVERY);self.assertEqual(self.c.status()['served_source'],2)
 def test_abort_and_first_activation_rollback(self):
  self.maintenance();self.c.call(m.BEGIN,struct.pack('<5I',0,1,len(self.image),zlib.crc32(self.image),9));self.c.call(m.CHUNK,struct.pack('<II',9,0)+self.image[:256]);self.c.call(m.ABORT)
  with self.assertRaises(m.Error):self.c.call(m.FINISH,struct.pack('<I',9))
  self.assertEqual(self.c.status()['upload_slot'],2);self.c.upload(0,1,self.image);self.c.call(m.ACTIVATE,struct.pack('<I',0));self.c.call(m.ROLLBACK);self.assertEqual(self.c.status()['served_source'],2)
 def test_reject_running_and_lost_isolation(self):
  with self.assertRaises(m.Error):self.c.upload(0,1,self.image)
  with self.assertRaises(m.Error):self.c.call(m.QUIESCE)
  self.maintenance();self.lib.rom_test_bus(0)
  with self.assertRaises(m.Error):self.c.upload(0,1,self.image)
  self.lib.rom_test_bus(1)
  with self.assertRaises(m.Error):self.c.upload(0,1,self.image)
  self.c.call(m.QUIESCE);self.c.upload(0,1,self.image)
 def test_interrupted_upload_and_reset(self):
  self.maintenance();self.c.upload(0,1,self.image);self.c.call(m.ACTIVATE,struct.pack('<I',0));self.c.call(m.BEGIN,struct.pack('<5I',1,2,len(self.image),zlib.crc32(self.image),9));self.c.call(m.CHUNK,struct.pack('<II',9,0)+self.image[:256]);self.lib.rom_test_reset(0)
  self.assertEqual(self.c.status()['selected_slot'],0);self.maintenance()
  with self.assertRaises(m.Error):self.c.call(m.FINISH,struct.pack('<I',9))
  self.assertFalse(self.c.status()['slot1_valid'])
 def test_power_loss_during_metadata_commit(self):
  self.maintenance();self.c.upload(0,1,self.image);self.c.call(m.ACTIVATE,struct.pack('<I',0));self.c.upload(1,2,self.image[:-2]);self.lib.rom_test_budget(1)
  with self.assertRaises(m.Error):self.c.call(m.ACTIVATE,struct.pack('<I',1))
  with self.assertRaises(m.Error):self.c.call(m.RESUME)
  self.lib.rom_test_budget(-1);self.c.call(m.QUIESCE);self.assertEqual(self.c.status()['served_source'],0)
  self.lib.rom_test_reset(0);self.assertEqual(self.c.status()['selected_slot'],0);self.assertEqual(self.c.status()['served_length'],514)
 def test_bad_crc_metadata_and_oversize(self):
  self.maintenance();self.c.call(m.BEGIN,struct.pack('<5I',0,1,512,0,9));self.c.call(m.CHUNK,struct.pack('<II',9,0)+self.image[:256]);self.c.call(m.CHUNK,struct.pack('<II',9,256)+self.image[256:512])
  with self.assertRaises(m.Error):self.c.call(m.FINISH,struct.pack('<I',9))
  self.c.upload(0,1,self.image);self.lib.rom_test_corrupt(0xfc0004);self.lib.rom_test_reset(0);self.maintenance()
  with self.assertRaises(m.Error):self.c.call(m.ACTIVATE,struct.pack('<I',0))
  for data,v in ((b'x'*65538,1),(self.image,0),(self.image[:-1],1)):
   before=self.t.calls
   with self.assertRaises(m.Error):self.c.upload(1,v,data)
   self.assertEqual(self.t.calls,before)
 def test_invalid_selected_payload_uses_recovery(self):
  self.maintenance();self.c.upload(0,1,self.image);self.c.call(m.ACTIVATE,struct.pack('<I',0));self.lib.rom_test_corrupt(0xfc1000);self.lib.rom_test_reset(0)
  s=self.c.status();self.assertEqual(s['selected_slot'],0);self.assertEqual(s['served_source'],2)
 def test_wrong_metadata_with_valid_record_crc(self):
  for field,value in ((4,2),(12,3),(16,0),(20,513),(16,99)):
   self.lib.rom_test_reset(1);self.maintenance();self.c.upload(0,1,self.image);self.c.call(m.ACTIVATE,struct.pack('<I',0));self.lib.rom_test_metadata(field,value);self.lib.rom_test_reset(0)
   self.assertEqual(self.c.status()['served_source'],2)
  self.assertEqual(self.c.status()['selected_slot'],0);self.maintenance()
  with self.assertRaises(m.Error):self.c.upload(0,2,self.image)
  self.c.upload(1,2,self.image);self.c.call(m.ACTIVATE,struct.pack('<I',1));self.assertEqual(self.c.status()['served_source'],1)
 def test_readback_compares_every_byte(self):
  self.maintenance();self.c.upload(0,1,self.image)
  with self.assertRaises(m.Error):self.c.verify(0,self.image[:-2])
 def test_frame_bounds_crc_version_and_request_ownership(self):
  for op,n in ((3,0),(4,8),(6,11),(7,0)):
   with self.assertRaises(m.Error):self.c.call(op,b'\0'*n)
  good=m.encode(1,42,b'abc');self.assertEqual(m.decode(good),(1,42,b'abc'))
  for off in (4,7,13,16):
   bad=bytearray(good);bad[off]^=1
   with self.assertRaises(m.Error):m.decode(bytes(bad))
  class Wrong:
   def exchange(self,frame):return m.encode(129,0,b'\0'*4)
  with self.assertRaises(m.Error):m.Client(Wrong()).status()
 def test_no_retry_on_ambiguous_completion(self):
  class Lost:
   calls=0
   def exchange(self,frame):self.calls+=1;raise m.Error('timeout')
  t=Lost()
  with self.assertRaises(m.Error):m.Client(t).call(m.ACTIVATE,struct.pack('<I',0))
  self.assertEqual(t.calls,1)
 def test_activation_applied_but_reply_lost(self):
  self.maintenance();self.c.upload(0,1,self.image)
  base=self.t
  class LostReply:
   calls=0
   def exchange(self,frame):
    self.calls+=1;reply=base.exchange(frame)
    if m.decode(frame)[0]==m.ACTIVATE:raise m.Error('reply lost after commit')
    return reply
  t=LostReply();client=m.Client(t)
  with self.assertRaises(m.Error):client.call(m.ACTIVATE,struct.pack('<I',0))
  self.assertEqual(t.calls,1);self.lib.rom_test_reset(0);self.assertEqual(self.c.status()['selected_slot'],0);self.assertEqual(self.c.status()['generation'],1)
 def test_legacy_usb_console_rejected_before_open(self):
  with patch.object(m,'check_usb_product',side_effect=m.Error('legacy')),patch.object(os,'open') as opened:
   with self.assertRaises(m.Error):m.Serial('/dev/example')
   opened.assert_not_called()
if __name__=='__main__':unittest.main()
