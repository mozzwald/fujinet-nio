#!/usr/bin/env python3
"""Dedicated RMU1 CDC client. No automatic flash, Prism reset, quiesce or resume.

Mutating requests are never retried: a timeout is an ambiguous completion.
Inspect status and re-enter maintenance explicitly before deciding what to do.
"""
import argparse,fcntl,hashlib,json,os,secrets,select,struct,sys,termios,time,zlib
from pathlib import Path
MAGIC=b'RMU1';MAX=512
STATUS,QUIESCE,BEGIN,CHUNK,FINISH,READ,ACTIVATE,ROLLBACK,RECOVERY,RESUME,ABORT,VERIFY=range(1,13)
FIELDS=('abi','flash_bytes','active','stopped','maintenance','core_held','selected_slot','generation','served_source','served_length','served_crc32','fallback_reason','upload_slot','received','storage_fault','owner_fault','slot0_valid','slot0_version','slot0_length','slot0_crc32','slot1_valid','slot1_version','slot1_length','slot1_crc32')
ERRORS=('ok','bad frame','invalid state','out of range','CRC mismatch','unsupported version','I/O failure: completion may be ambiguous','bus isolation required or lost','ownership/rollback conflict')
class Error(RuntimeError):pass

def encode(op,seq,payload=b''):
 if len(payload)>MAX:raise Error('payload too large')
 head=struct.pack('<4sBBHI',MAGIC,1,op,len(payload),seq)
 return head+struct.pack('<I',zlib.crc32(head+payload))+payload

def decode(frame):
 if len(frame)<16:raise Error('short frame')
 magic,version,op,n,seq,crc=struct.unpack_from('<4sBBHII',frame)
 if magic!=MAGIC or version!=1 or n>MAX or len(frame)!=16+n:raise Error('frame identity/length')
 if zlib.crc32(frame[:12]+frame[16:])!=crc:raise Error('frame CRC')
 return op,seq,frame[16:]

def check_usb_product(port,root=Path('/sys/class/tty')):
 device=(root/Path(port).resolve().name/'device').resolve()
 for parent in (device,*device.parents):
  try:
   identity=((parent/'idVendor').read_text().strip(),(parent/'idProduct').read_text().strip(),(parent/'product').read_text().strip())
  except OSError:continue
  if identity!=('2e8a','0009','FujiNet ROM Manager v1'):raise Error('wrong USB product; no bytes sent to legacy/NIO console')
  return
 raise Error('cannot verify dedicated USB ROM manager identity; no bytes sent')

class Serial:
 def __init__(self,port,timeout=10):
  check_usb_product(port)
  self.fd=os.open(port,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK);self.timeout=timeout;self.pending=bytearray()
  try:
   fcntl.flock(self.fd,fcntl.LOCK_EX|fcntl.LOCK_NB);fcntl.ioctl(self.fd,termios.TIOCEXCL)
   attrs=termios.tcgetattr(self.fd);attrs[0]=attrs[1]=attrs[3]=0;attrs[2]=termios.CS8|termios.CREAD|termios.CLOCAL;attrs[4]=attrs[5]=termios.B115200;attrs[6][termios.VMIN]=attrs[6][termios.VTIME]=0;termios.tcsetattr(self.fd,termios.TCSANOW,attrs)
   fcntl.ioctl(self.fd,termios.TIOCMBIS,struct.pack('I',termios.TIOCM_DTR))
  except BaseException:self.close();raise
 def close(self):
  if self.fd is not None:
   try:fcntl.ioctl(self.fd,termios.TIOCNXCL)
   finally:os.close(self.fd);self.fd=None
 def exchange(self,frame):
  deadline=time.monotonic()+self.timeout;data=frame
  while data:
   if not select.select([], [self.fd], [],max(0,deadline-time.monotonic()))[1]:raise Error('write timeout; inspect state before retry')
   try:n=os.write(self.fd,data)
   except BlockingIOError:continue
   if not n:raise Error('USB disconnected')
   data=data[n:]
  while True:
   pos=self.pending.find(MAGIC)
   if pos<0:self.pending[:]=self.pending[-3:]
   else:
    if pos:del self.pending[:pos]
    if len(self.pending)>=16:
     n=int.from_bytes(self.pending[6:8],'little')
     if n>MAX:raise Error('oversized response')
     if len(self.pending)>=16+n:
      result=bytes(self.pending[:16+n]);del self.pending[:16+n];return result
   remaining=deadline-time.monotonic()
   if remaining<=0:raise Error('response timeout; completion ambiguous, no retry sent')
   if select.select([self.fd],[],[],remaining)[0]:
    b=os.read(self.fd,4096)
    if not b:raise Error('USB disconnected; inspect state before retry')
    self.pending.extend(b)

class Client:
 def __init__(self,transport):self.transport=transport;self.seq=secrets.randbelow(0xfffffffe)+1
 def call(self,op,payload=b''):
  seq=self.seq;self.seq=(seq+1)&0xffffffff
  code,reply_seq,p=decode(self.transport.exchange(encode(op,seq,payload)))
  if code!=op|128 or reply_seq!=seq or len(p)<4:raise Error('wrong reply owner/opcode')
  error=struct.unpack_from('<I',p)[0]
  if error:raise Error(ERRORS[error] if error<len(ERRORS) else f'unknown error {error}')
  return p[4:]
 def status(self):
  b=self.call(STATUS)
  if len(b)!=96:raise Error('status schema length')
  d=dict(zip(FIELDS,struct.unpack('<24I',b)))
  if d['abi']!=1:raise Error('status schema version')
  return d
 def maintenance(self):
  s=self.status()
  if not s['maintenance'] or not s['core_held'] or s['active'] or s['flash_bytes']!=0x1000000 or s['storage_fault']:raise Error('explicit quiesce with held hardware RESET is required')
  return s
 def verify(self,slot,data=None):
  s=self.maintenance();self.call(VERIFY,struct.pack('<I',slot));s=self.status();n=s[f'slot{slot}_length'];crc=s[f'slot{slot}_crc32'];got=bytearray()
  if not 512<=n<=65536 or n&1:raise Error('bad slot length')
  for off in range(0,n,256):
   count=min(256,n-off);b=self.call(READ,struct.pack('<III',slot,off,count))
   if len(b)!=count:raise Error('short readback')
   got.extend(b)
  if zlib.crc32(got)!=crc or data is not None and got!=data:raise Error('independent readback mismatch')
  return dict(slot=slot,version=s[f'slot{slot}_version'],length=n,crc32=f'{crc:08x}',sha256=hashlib.sha256(got).hexdigest(),activated=False)
 def upload(self,slot,version,data):
  validate(data,version);s=self.maintenance()
  if slot==s['selected_slot']:raise Error('cannot overwrite selected image')
  nonce=secrets.randbelow(0xffffffff)+1
  self.call(BEGIN,struct.pack('<5I',slot,version,len(data),zlib.crc32(data),nonce))
  for off in range(0,len(data),256):self.call(CHUNK,struct.pack('<II',nonce,off)+data[off:off+256])
  self.call(FINISH,struct.pack('<I',nonce))
  return self.verify(slot,data)

def validate(data,version):
 if not 0<version<=0xffffffff or not 512<=len(data)<=65536 or len(data)&1:raise Error('require nonzero32-bit image version and even512..65536-byte ROM')
 if data[8:12]!=bytes.fromhex('feedbeef'):raise Error('missing Palm card signature at offset8')

def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--port',required=True);a.add_argument('--timeout',type=float,default=10)
 sub=a.add_subparsers(dest='command',required=True)
 for cmd in ('status','quiesce','rollback','recovery','resume','abort'):sub.add_parser(cmd)
 for cmd in ('upload','verify','activate'):
  p=sub.add_parser(cmd);p.add_argument('--slot',type=int,choices=(0,1),required=True)
  if cmd=='upload':p.add_argument('--image',type=Path,required=True);p.add_argument('--image-version',type=int,required=True)
  if cmd=='verify':p.add_argument('--image',type=Path)
 x=a.parse_args();serial=None
 try:
  if x.timeout<=0:raise Error('positive timeout required')
  data=x.image.read_bytes() if x.command in ('upload','verify') and x.image else None
  if x.command=='upload':validate(data,x.image_version)
  serial=Serial(x.port,x.timeout);c=Client(serial)
  # First exchange must identify our dedicated protocol, never a legacy console.
  c.status()
  if x.command=='status':result=c.status()
  elif x.command=='upload':result=c.upload(x.slot,x.image_version,data)
  elif x.command=='verify':result=c.verify(x.slot,data)
  else:
   op={'quiesce':QUIESCE,'activate':ACTIVATE,'rollback':ROLLBACK,'recovery':RECOVERY,'resume':RESUME,'abort':ABORT}[x.command]
   c.call(op,struct.pack('<I',x.slot) if x.command=='activate' else b'');result=c.status()
  print(json.dumps(result,indent=2));return 0
 except (OSError,Error) as e:print(f'INCOMPLETE: {e}. No automatic retry/reset/resume.',file=sys.stderr);return 1
 finally:
  if serial:serial.close()
if __name__=='__main__':raise SystemExit(main())
