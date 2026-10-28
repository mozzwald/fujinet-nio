"""Palm write-ISSUED journal; actual acceptance requires separate RP capture."""
import struct
import zlib

FIELDS = ('magic version sequence mode error card generation cs0 cs_size cs1 access_ns '
          'ram_pc requested completed cs0_reads writes_issued intended_value reserved '
          'offset reserved2 ram_proof ticks seconds crc32').split()


def decode_record(raw):
    if len(raw) != 96: raise ValueError('record length')
    r = dict(zip(FIELDS, struct.unpack('>24I', raw)))
    if r['magic'] != 0x46574533 or r['version'] != 1: raise ValueError('record identity')
    if zlib.crc32(raw[:92]) != r['crc32']: raise ValueError('record CRC')
    if r['mode'] not in (1, 2): raise ValueError('unknown mode')
    value, offset = (0x1234, 0) if r['mode'] == 1 else (0xa55a, 2)
    r['issued'] = bool(r['sequence'] > 0 and not r['error'] and
        r['ram_proof'] == 1 and 1 <= r['card'] <= 3 and r['generation'] > 0 and
        r['cs_size'] == 0x1000000 and r['cs0'] and not r['cs0'] & 0xffffff and
        r['cs1'] == r['cs0'] + r['cs_size'] <= 0xffffffff and r['access_ns'] == 330 and
        r['requested'] == r['completed'] == r['writes_issued'] == 1 and
        r['cs0_reads'] == r['reserved'] == r['reserved2'] == 0 and
        r['intended_value'] == value and r['offset'] == offset)
    r['rp_capture_verified'] = False
    return r


def pdb_records(data):
    if len(data) < 78 or data[:32].split(b'\0')[0] != b'FN Write Capture E3' or data[60:68] != b'DATAFNFW':
        raise ValueError('PDB identity')
    if int.from_bytes(data[32:34], 'big') & 1: raise ValueError('resource DB')
    count = int.from_bytes(data[76:78], 'big'); end = 78 + 8 * count
    if not count or len(data) < end: raise ValueError('record table')
    offsets = [int.from_bytes(data[78+i*8:82+i*8], 'big') for i in range(count)]
    if any(o < end or o >= len(data) for o in offsets) or offsets != sorted(set(offsets)):
        raise ValueError('record offsets')
    return [data[o:offsets[i+1] if i+1 < count else len(data)] for i, o in enumerate(offsets)]
