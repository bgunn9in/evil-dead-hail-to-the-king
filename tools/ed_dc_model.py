"""Read checked Dreamcast mesh arrays and embedded original PVRT resources."""

import hashlib
import math
import struct

from ed_pvr import decode_pvr


def decode_mesh(data):
    """The DC part stores XYZUV, then normals, then bones and triangle indices."""
    if len(data) < 8:
        raise ValueError('Truncated Dreamcast mesh table')
    count = struct.unpack_from('<I', data)[0]
    if not 1 <= count <= 256 or count > (len(data) - 4) // 4:
        raise ValueError('Invalid Dreamcast mesh part count')
    offsets = list(struct.unpack_from(f'<{count}I', data, 4)) + [len(data)]
    if offsets[0] != 4 + count * 4:
        raise ValueError('Unexpected Dreamcast mesh table size')
    parts = []
    for start, end in zip(offsets, offsets[1:]):
        if start % 4 or end % 4 or end - start < 16 or end > len(data):
            raise ValueError('Invalid Dreamcast mesh part range')
        flags, vertices, assignments, triangles = struct.unpack_from('<4I', data, start)
        if not vertices or assignments != vertices:
            raise ValueError('Unsupported Dreamcast vertex/bone assignment counts')
        payload_end = start + 16 + vertices * 32 + assignments * 2 + triangles * 6
        if (payload_end + 3) & ~3 != end:
            raise ValueError('Dreamcast mesh counts exceed its range')
        vertex_end = start + 16 + vertices * 20
        normal_end = vertex_end + vertices * 12
        records = list(struct.iter_unpack('<5f', data[start + 16:vertex_end]))
        normals = list(struct.iter_unpack('<3f', data[vertex_end:normal_end]))
        if any(not math.isfinite(x) for row in records + normals for x in row):
            raise ValueError('Non-finite Dreamcast mesh coordinate')
        bones = list(struct.unpack_from(f'<{assignments}H', data, normal_end))
        face_offset = normal_end + assignments * 2
        faces = [list(face) for face in struct.iter_unpack('<3H', data[face_offset:payload_end])]
        if any(index >= vertices for face in faces for index in face):
            raise ValueError('Dreamcast triangle index is outside the vertex array')
        parts.append({'flags_raw': flags, 'positions': [list(row[:3]) for row in records],
                      'uv': [list(row[3:]) for row in records], 'normals': [list(row) for row in normals],
                      # No vertex-color array exists in the DC source part.
                      'colors': [0xffffffff] * vertices, 'bone_indices': bones, 'triangles': faces})
    return parts


def decode_texture(data, identity):
    """Decode only the declared PVRT chunk; report and retain the opaque tail."""
    if len(data) < 32 or struct.unpack_from('<4I', data) != (identity, 0, 0, 0):
        raise ValueError('Invalid Dreamcast embedded texture prefix')
    size = struct.unpack_from('<I', data, 20)[0] + 8
    if size < 16 or size > len(data) - 16:
        raise ValueError('Dreamcast embedded PVRT exceeds resource bounds')
    pvr, tail = data[16:16 + size], data[16 + size:]
    image = decode_pvr(pvr)
    return image, {'resource': f'{identity:08x}', 'pixel': pvr[8], 'layout': pvr[9],
                   'width': image.width, 'height': image.height, 'pvrt_bytes': size,
                   'pvrt_sha256': hashlib.sha256(pvr).hexdigest(), 'tail_bytes': len(tail),
                   'tail_sha256': hashlib.sha256(tail).hexdigest(), 'tail_nonzero': any(tail),
                   'tail_status': 'Preserved in the complete source resource; not interpreted or uploaded.'}
