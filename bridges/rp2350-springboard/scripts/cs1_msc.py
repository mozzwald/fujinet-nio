#!/usr/bin/env python3
"""CS1 ROM-drive identity/status/recovery. No flash, ROM activation or reset.

Recovery briefly clamps/restarts bus service; use only after an explicit idle cue.
"""
import argparse,hashlib,json,time
from pathlib import Path
PRODUCT='FujiNet CS1 ROM Drive A1'
VID,PID=0xcafe,0x4014

def check_port(port,ports):
 target=Path(port).resolve()
 rows=[p for p in ports if Path(p.device).resolve()==target]
 if len(rows)!=1 or (rows[0].vid,rows[0].pid,rows[0].product)!=(VID,PID,PRODUCT):
  raise ValueError('refusing unexpected USB device before sending bytes')
 return rows[0].device

def verify_bundle(path):
 b=json.loads(path.read_text())
 if b.get('kind')!='cs1-msc-stage-a':raise ValueError('wrong bundle kind')
 for item in b['artifacts'].values():
  p=(path.parent/item['path']).resolve()
  if not p.is_relative_to(path.parent.resolve()) or hashlib.sha256(p.read_bytes()).hexdigest()!=item['sha256']:
   raise ValueError('bundle artifact path/hash mismatch')
 return b

def validate(r,bundle=None,ready=False):
 if r.get('product')!=PRODUCT:raise ValueError('unexpected firmware product')
 if bundle:
  if r.get('build_id')!=bundle['build_id']:raise ValueError('firmware build mismatch')
  if (r.get('served_length'),r.get('served_crc32'),r.get('served_version'),r.get('import_result'))!=(bundle['rom_length'],bundle['rom_crc32'],bundle['rom_version'],0):
   raise ValueError('intended module ROM is not active; do not run Palm diagnostic')
 if r.get('clock_khz')!=150000:raise ValueError('unexpected bus service clock')
 if not r.get('ram_island') or not r.get('flash_size_ok') or r.get('exception_fault') or r.get('disk_fault'):
  raise ValueError('ROM-drive safety/health gate failed')
 if ready:
  c=r['cs1']
  if (c.get('id'),c.get('abi'),c.get('capabilities'))!=(0x464e,0x100,1):raise ValueError('wrong CS1 table ABI')
  if not r['active'] or c['stopped'] or c['arm_pending'] or c['status']!=1 or c['error'] or c['capture_fault'] or c['read_fault'] or c['ack'] or c['accepted']:
   raise ValueError('CS1 session is not fresh/ready; keep Palm idle')
 return r

class Client:
 def __init__(self,port):
  import serial
  from serial.tools import list_ports
  device=check_port(port,list_ports.comports())
  self.serial=serial.Serial(device,115200,timeout=3,write_timeout=3,exclusive=True)
  try:check_port(device,list_ports.comports())
  except Exception:self.serial.close();raise
  self.serial.reset_input_buffer();self.raw_lines=[]
 def send(self,command):
  if command not in ('status','recover'):raise ValueError('invalid management command')
  self.serial.write((command+'\n').encode());self.serial.flush()
  line=self.serial.readline();self.raw_lines.append(line.hex())
  return json.loads(line)
 def close(self):self.serial.close()

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('mode',choices=('status','ready','recover'))
 p.add_argument('--port',required=True);p.add_argument('--bundle',type=Path)
 p.add_argument('--client-idle',action='store_true',help='explicit confirmation for recovery outage')
 p.add_argument('--output',type=Path,required=True)
 a=p.parse_args()
 if a.output.exists():p.error('output must be new to preserve evidence')
 if a.mode!='status' and not a.bundle:p.error('ready/recover require exact candidate bundle')
 if a.mode=='recover' and not a.client_idle:p.error('recovery needs an explicit quiescent-client cue')
 bundle=verify_bundle(a.bundle) if a.bundle else None
 client=Client(a.port);rows=[]
 try:
  r=client.send('status');rows.append(r);validate(r,bundle)
  if a.mode=='recover':
   if not r['active'] and not r['cs1']['stopped']:raise ValueError('bus owner is in transition')
   run=r['cs1']['run'];rows.append(client.send('recover'))
   deadline=time.monotonic()+5
   while time.monotonic()<deadline:
    time.sleep(.05);r=client.send('status');rows.append(r)
    if r['cs1']['run']==run+1 and r['active'] and not r['cs1']['arm_pending']:break
   else:raise ValueError('recovery not confirmed; no automatic retry')
  validate(r,bundle,ready=a.mode!='status')
 finally:
  client.close();a.output.write_text(json.dumps({'mode':a.mode,'responses':rows,'raw_lines_hex':client.raw_lines,'hardware_reset':False,'firmware_flash':False},indent=2)+'\n')
 print(json.dumps(r,indent=2))
if __name__=='__main__':main()
