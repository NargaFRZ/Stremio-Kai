"""Report native release identity when the documented base does not match."""
import hashlib
import json
from pathlib import Path
import re
import struct
import sys


def inspect(path):
    path = Path(path)
    raw = path.read_bytes()
    pe = struct.unpack_from('<I', raw, 0x3c)[0]
    assert raw[pe:pe+4] == b'PE\0\0'
    machine, count, timestamp = struct.unpack_from('<HHI', raw, pe+4)
    optional_size = struct.unpack_from('<H', raw, pe+20)[0]
    sections = []
    for index in range(count):
        offset = pe+24+optional_size+index*40
        name = raw[offset:offset+8].rstrip(b'\0').decode('ascii')
        virtual_size, rva, size, start = struct.unpack_from('<IIII', raw, offset+8)
        section = raw[start:start+size]
        sections.append({'name': name, 'virtual_size': virtual_size, 'rva': rva,
                         'raw_size': size, 'sha256': hashlib.sha256(section).hexdigest()})
    strings = [s.decode('ascii') for s in re.findall(rb'[\x20-\x7e]{5,}', raw)]
    interesting = [s for s in strings if '.pdb' in s.lower() or
                   re.fullmatch(r'5\.0\.\d{1,3}', s) or '[DISCORD]' in s]
    return {'file': path.name, 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest(),
            'machine': hex(machine), 'link_timestamp': timestamp,
            'sections': sections, 'identity_strings': interesting}


if __name__ == '__main__':
    print(json.dumps([inspect(path) for path in sys.argv[1:]], indent=2))
