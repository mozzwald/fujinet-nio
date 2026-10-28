#!/usr/bin/env python3
"""Actual portable MSC storage/parser/private-slot integration; no hardware."""
import argparse
import ctypes as C
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import shutil,subprocess,tempfile
import zlib
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('rom_msc',ROOT/'scripts/rom_msc.py')
rom_msc=importlib.util.module_from_spec(spec);spec.loader.exec_module(rom_msc)
class Image(C.Structure):
    _fields_=[(k,C.c_uint32) for k in ('version','length','crc')]

def disk_file(data,fragmented=False):
    b=bytearray(1048576)
    b[450]=1;struct.pack_into('<II',b,454,1,2047);b[510:512]=b'\x55\xaa'
    v=memoryview(b)[512:1024];v[:11]=b'\xeb\x3c\x90MSDOS5.0'
    for off,n in ((11,512),(14,1),(17,64),(19,2047),(22,6),(24,32),(26,64)):
        struct.pack_into('<H',v,off,n)
    v[13]=1;v[16]=2;v[21]=0xf8;struct.pack_into('<I',v,28,1);v[510:512]=b'\x55\xaa'
    fat=bytearray(3072);fat[:3]=b'\xf8\xff\xff'
    chain=[2+i*(3 if fragmented else 1) for i in range((len(data)+511)//512)]
    for i,c in enumerate(chain):
        n=chain[i+1] if i+1<len(chain) else 0xfff
        off=c+c//2;prior=int.from_bytes(fat[off:off+2],'little')
        struct.pack_into('<H',fat,off,((prior&15)|(n<<4)) if c&1 else ((prior&0xf000)|n))
        at=(18+c-2)*512;b[at:at+512]=data[i*512:(i+1)*512].ljust(512,b'\0')
    b[1024:4096]=fat;b[4096:7168]=fat
    at=14*512;b[at:at+11]=b'SPRINGBDROM';b[at+11]=0x20
    struct.pack_into('<H',b,at+26,2);struct.pack_into('<I',b,at+28,len(data))
    return b

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('library',type=Path);p.add_argument('rom',type=Path)
    p.add_argument('--second-rom',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();lib=C.CDLL(str(a.library.resolve()));lib.fixture_media.restype=C.c_void_p
    lib.fixture_write.argtypes=[C.c_uint,C.c_char_p]
    lib.fixture_load.argtypes=[C.c_void_p,C.POINTER(Image)]
    lib.fixture_promote.argtypes=[C.c_void_p,Image]
    lib.fixture_restore.argtypes=[C.c_char_p]
    lib.fixture_active.argtypes=[C.c_void_p]
    cases=[];out=C.create_string_buffer(65568);info=Image()
    raw=a.rom.read_bytes();other=a.second_rom.read_bytes();pkg=rom_msc.package(raw,1)
    def load(volume):
        lib.fixture_blank()
        for l in range(2048):
            assert lib.fixture_write(l,bytes(volume[l*512:(l+1)*512]))
        assert lib.fixture_restart()
        return lib.fixture_load(out,C.byref(info))
    # Exercise a real PC FAT writer against the actual synthesized empty disk.
    if shutil.which('mcopy') and shutil.which('fsck.fat'):
        lib.fixture_blank();seed=bytearray();sector=C.create_string_buffer(512)
        for l in range(2048):
            assert lib.fixture_read(l,sector);seed.extend(sector.raw)
        with tempfile.TemporaryDirectory(prefix='rom-msc-pc-') as temp:
            t=Path(temp);image=t/'disk.img';image.write_bytes(seed)
            file=t/'SPRINGBD.ROM';file.write_bytes(pkg)
            subprocess.run(['mcopy','-i',str(image)+'@@512',str(file),'::SPRINGBD.ROM'],check=True,capture_output=True)
            logical=image.read_bytes();partition=t/'partition.img';partition.write_bytes(logical[512:])
            subprocess.run(['fsck.fat','-n',str(partition)],check=True,capture_output=True)
            assert load(logical)==0 and out.raw[:info.length]==raw
        cases.append('PC-mcopy-fsck-native-import')
    for fragment in (False,True):
        assert load(disk_file(pkg,fragment))==0
        assert out.raw[:info.length]==raw
        assert info.version==1 and info.crc==zlib.crc32(raw)
        cases.append('fragmented' if fragment else 'contiguous')
    for name,mutate,expected in [
        ('deleted',lambda b:b.__setitem__(7168,0xe5),1),
        ('missing',lambda b:b.__setitem__(7168,0),1),
        ('directory',lambda b:b.__setitem__(7179,0x10),4),
        ('wrong-name',lambda b:b.__setitem__(7168,ord('X')),1),
        ('partition-bounds',lambda b:struct.pack_into('<I',b,454,2048),3),
        ('bpb-sector',lambda b:struct.pack_into('<H',b,523,1024),3),
        ('fat-mismatch',lambda b:b.__setitem__(4096+3,b[4099]^1),3),
        ('duplicate',lambda b:b.__setitem__(slice(7200,7232),b[7168:7200]),4),
        ('oversize',lambda b:struct.pack_into('<I',b,7196,65570),4),
        ('short-chain',lambda b:(struct.pack_into('<H',b,1027,0xfff),struct.pack_into('<H',b,4099,0xfff)),4),
        ('chain-loop',lambda b:(struct.pack_into('<H',b,1027,2),struct.pack_into('<H',b,4099,2)),4),
    ]:
        b=disk_file(pkg);mutate(b);result=load(b);assert result==expected,(name,result);cases.append(name)
    for name,off,value in [('schema',4,2),('version-zero',8,0),('declared-size',12,65538),('reserved',20,1),('header-crc',28,0)]:
        b=bytearray(pkg);struct.pack_into('<I',b,off,value)
        if off!=28:struct.pack_into('<I',b,28,zlib.crc32(b[:28]))
        assert load(disk_file(b))==5,name;cases.append(name)
    b=bytearray(pkg);b[-1]^=1;assert load(disk_file(b))==5;cases.append('payload-crc')
    assert load(disk_file(pkg[:-2]))==5;cases.append('truncated-package')
    # Valid outer CRC but broken Palm database resource pointer must fail.
    b=bytearray(raw);directory=int.from_bytes(b[316:320],'big')-0x8000000
    db=int.from_bytes(b[directory+6:directory+10],'big')-0x8000000
    struct.pack_into('>I',b,db+84,0x7ffffffe)
    import binascii
    struct.pack_into('>H',b,112,binascii.crc_hqx(b[114:],binascii.crc_hqx(b[:112],0)))
    assert load(disk_file(rom_msc.package(bytes(b),1)))==6;cases.append('resource-outside-rom')
    # Maximum legal payload crosses the 64KiB file boundary due to envelope.
    import binascii
    maximum=bytearray(raw.ljust(65536,b'\xff'))
    struct.pack_into('>I',maximum,108,len(maximum))
    struct.pack_into('>H',maximum,112,binascii.crc_hqx(maximum[114:],binascii.crc_hqx(maximum[:112],0)))
    assert load(disk_file(rom_msc.package(bytes(maximum),3),True))==0
    assert info.length==65536 and out.raw[:65536]==maximum
    cases.append('maximum-65536-payload-fragmented')
    # Prepare private A, then promote B under every operation boundary/cut.
    lib.fixture_blank();first=Image(1,len(raw),zlib.crc32(raw));second=Image(2,len(other),zlib.crc32(other))
    assert lib.fixture_promote(raw,first)==0
    before=lib.fixture_generation();assert lib.fixture_promote(raw,first)==0
    assert lib.fixture_generation()==before;cases.append('same-image-no-flash-rewrite')
    snapshot=C.string_at(lib.fixture_media(),0x1000000)
    lib.fixture_fail(-1,0);assert lib.fixture_promote(other,second)==0
    operations=lib.fixture_operations();assert operations>40
    interruptions=0
    for op in range(operations):
        for cut in (0,1,31,128,255,256,4096):
            lib.fixture_restore(snapshot);lib.fixture_fail(op,cut)
            assert lib.fixture_promote(other,second)==7
            assert lib.fixture_restart()
            assert lib.fixture_active(out)
            assert out.raw[:len(raw)]==raw or out.raw[:len(other)]==other,(op,cut)
            interruptions+=1
    cases.append('interrupted-promotion-old-or-new')
    # Successful second image, then normal file-based rollback and restart.
    lib.fixture_restore(snapshot);assert lib.fixture_promote(other,second)==0
    assert lib.fixture_restart() and lib.fixture_active(out) and out.raw[:len(other)]==other
    assert lib.fixture_promote(raw,first)==0
    assert lib.fixture_restart() and lib.fixture_active(out) and out.raw[:len(raw)]==raw
    cases.append('rollback-with-previous-file')
    a.output.write_text(json.dumps(dict(result='PASS',cases=cases,count=len(cases),promotion_power_cuts=interruptions,
        payloads=[dict(bytes=len(r),sha256=hashlib.sha256(r).hexdigest()) for r in (raw,other)],
        scope='Actual native C COW/FAT/package/private-slot code; not USB or physical flash qualification'),indent=2)+'\n')
    print(f'PASS {len(cases)} integration cases and {interruptions} interrupted private-slot promotions')
if __name__=='__main__':main()
