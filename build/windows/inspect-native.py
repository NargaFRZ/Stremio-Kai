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
    optional = pe+24
    magic = struct.unpack_from('<H', raw, optional)[0]
    assert magic == 0x20b, 'Expected Windows x64 PE32+'
    directories = [struct.unpack_from('<II', raw, optional+112+i*8) for i in range(16)]
    sections = []
    for index in range(count):
        offset = pe+24+optional_size+index*40
        name = raw[offset:offset+8].rstrip(b'\0').decode('ascii')
        virtual_size, rva, size, start = struct.unpack_from('<IIII', raw, offset+8)
        section = raw[start:start+size]
        sections.append({'name': name, 'virtual_size': virtual_size, 'rva': rva,
                         'raw_size': size, 'flags': struct.unpack_from('<I', raw, offset+36)[0],
                         'sha256': hashlib.sha256(section).hexdigest()})
    strings = [s.decode('ascii') for s in re.findall(rb'[\x20-\x7e]{5,}', raw)]
    interesting = [s for s in strings if '.pdb' in s.lower() or
                   re.fullmatch(r'5\.0\.\d{1,3}', s) or '[DISCORD]' in s]
    return {'file': path.name, 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest(),
            'machine': hex(machine), 'link_timestamp': timestamp,
            'entrypoint': struct.unpack_from('<I', raw, optional+16)[0],
            'image_base': struct.unpack_from('<Q', raw, optional+24)[0],
            'subsystem': struct.unpack_from('<H', raw, optional+68)[0],
            'dll_characteristics': struct.unpack_from('<H', raw, optional+70)[0],
            'directories': directories,
            'sections': sections, 'identity_strings': interesting}


def verify_resource_variant(kai, community):
    for key in ('machine', 'entrypoint', 'image_base', 'subsystem', 'dll_characteristics'):
        if kai[key] != community[key]:
            raise RuntimeError(f'Native loader field differs: {key}')
    left = {s['name']: s for s in kai['sections']}
    right = {s['name']: s for s in community['sections']}
    if set(left) != set(right) or '.text' not in left or '.rsrc' not in left:
        raise RuntimeError('Native section layout differs')
    for name, section in left.items():
        reference = right[name]
        if section['flags'] != reference['flags']:
            raise RuntimeError(f'Native section flags differ: {name}')
        if name == '.rsrc':
            if section['flags'] & 0x20000000:
                raise RuntimeError('Resource section must not contain executable code')
            continue
        for key in ('virtual_size', 'raw_size', 'sha256'):
            if section[key] != reference[key]:
                raise RuntimeError(f'Native code/data differs: {name} {key}')
        if name != '.reloc' and section['rva'] != reference['rva']:
            raise RuntimeError(f'Native code/data address differs: {name}')
    # Resources and the relocation table can move when resources grow. Security
    # directory is an on-disk signature location, not application code.
    for index, (a, b) in enumerate(zip(kai['directories'], community['directories'])):
        if index not in (2, 4, 5) and a != b:
            raise RuntimeError(f'Native data directory differs: {index}')


if __name__ == '__main__':
    if sys.argv[1:2] == ['--verify-resource-variant']:
        kai, community = [inspect(path) for path in sys.argv[2:]]
        verify_resource_variant(kai, community)
        print('Verified: all non-resource native sections match Community 5.0.21 byte for byte.')
    else:
        print(json.dumps([inspect(path) for path in sys.argv[1:]], indent=2))
