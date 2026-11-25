#!/usr/bin/env python3
"""Make a GameSprout linear-skin derivative of a binary FBX source."""

import argparse
import struct
from pathlib import Path


MAGIC = b'Kaydara FBX Binary  \x00\x1a\x00'
SKINNING = {b'S\x05\0\0\0Blend', b'S\x0e\0\0\0DualQuaternion'}
LINEAR = b'S\x06\0\0\0Linear'


def linear_skin(source):
    if not source.startswith(MAGIC) or len(source) < 27:
        raise ValueError('Expected binary FBX source')
    version, = struct.unpack_from('<I', source, 23)
    header = struct.Struct('<QQQB' if version >= 7500 else '<IIIB')
    output = bytearray(source[:27])
    changed = 0

    def copy_node(offset):
        nonlocal changed
        end, count, properties_size, name_size = header.unpack_from(source, offset)
        body_start = offset + header.size
        if end == 0:
            output.extend(source[offset:body_start])
            return body_start, True
        name_end = body_start + name_size
        data_end = name_end + properties_size
        if not (body_start < name_end <= data_end <= end <= len(source)):
            raise ValueError(f'Malformed FBX node at byte {offset}')
        name = source[body_start:name_end]
        properties = source[name_end:data_end]
        if name == b'SkinningType' and count == 1 and properties in SKINNING:
            properties = LINEAR
            changed += 1
        header_offset = len(output)
        output.extend(bytes(header.size))
        output.extend(name)
        output.extend(properties)
        child = data_end
        while child < end:
            child, is_end = copy_node(child)
            if is_end:
                break
        if child != end:
            raise ValueError(f'Unexpected end of FBX node {name!r}')
        header.pack_into(output, header_offset, len(output), count, len(properties), name_size)
        return end, False

    offset = 27
    while offset < len(source):
        offset, is_end = copy_node(offset)
        if is_end:
            break
    output.extend(source[offset:])
    return output, changed


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('derived', type=Path)
    args = parser.parse_args()
    if args.source.resolve() == args.derived.resolve():
        parser.error('Source and derivative must be separate files')
    data, count = linear_skin(args.source.read_bytes())
    args.derived.write_bytes(data)
    print(f'{count} skin deformer(s) set to Linear: {args.derived}')
