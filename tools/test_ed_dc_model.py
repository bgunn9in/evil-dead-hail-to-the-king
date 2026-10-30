import math
from pathlib import Path
import struct
import unittest

from ed_assets import parse_dc_archive
from ed_dc_model import decode_mesh, decode_texture


ROOT = Path(__file__).resolve().parents[1]
ORIGINAL = ROOT / 'build/dreamcast-original-20261027/files'


def mesh():
    data = bytearray(struct.pack('<6I', 1, 8, 0, 3, 3, 1))
    data.extend(struct.pack('<15f', 1, 2, 3, .1, .2, 4, 5, 6, .3, .4, 7, 8, 9, .5, .6))
    data.extend(struct.pack('<9f', 0, 1, 0, 0, 0, 1, 1, 0, 0))
    data.extend(struct.pack('<6H', 65535, 65535, 65535, 0, 2, 1))
    return data


class DcModelTests(unittest.TestCase):
    def test_separate_vertex_normal_and_index_arrays(self):
        part = decode_mesh(mesh())[0]
        self.assertEqual(part['positions'], [[1, 2, 3], [4, 5, 6], [7, 8, 9]])
        self.assertEqual(part['normals'], [[0, 1, 0], [0, 0, 1], [1, 0, 0]])
        self.assertAlmostEqual(part['uv'][1][0], .3)
        self.assertEqual(part['triangles'], [[0, 2, 1]])
        self.assertEqual(part['colors'], [0xffffffff] * 3)

    def test_reject_malformed_arrays_and_coordinates(self):
        good = mesh()
        malformed = [b'', good[:-1], good + b'\0' * 4]
        for offset, value in ((0, 257), (4, 4), (12, 4), (16, 2), (20, 2)):
            bad = bytearray(good); struct.pack_into('<I', bad, offset, value); malformed.append(bad)
        bad = bytearray(good); struct.pack_into('<f', bad, 24, math.nan); malformed.append(bad)
        bad = bytearray(good); struct.pack_into('<H', bad, len(bad) - 2, 3); malformed.append(bad)
        for data in malformed:
            with self.assertRaises(ValueError):
                decode_mesh(data)

    def test_embedded_texture_keeps_unknown_nonzero_tail(self):
        pvr = struct.pack('<4sIBBHHH4H', b'PVRT', 16, 2, 1, 0, 2, 2, 0xf000, 0xffff, 0, 0x8888)
        raw = struct.pack('<4I', 0x08024000, 0, 0, 0) + pvr + b'\x12\x34\0'
        image, info = decode_texture(raw, 0x08024000)
        self.assertEqual(image.size, (2, 2))
        self.assertEqual(info['tail_bytes'], 3)
        self.assertTrue(info['tail_nonzero'])
        for invalid in (raw[:31], raw, raw[:20] + b'\xff\xff\xff\xff' + raw[24:]):
            with self.assertRaises(ValueError):
                decode_texture(invalid, 0x08024001 if invalid is raw else 0x08024000)

    @unittest.skipUnless(ORIGINAL.exists(), 'Original extracted Dreamcast corpus is local-only')
    def test_actual_mesh_and_texture_corpus(self):
        meshes, textures, parts = 0, 0, 0
        for path in ORIGINAL.glob('*.DC'):
            raw = path.read_bytes()
            for entry in parse_dc_archive(raw):
                kind = entry.resource_id >> 24
                data = raw[entry.offset:entry.offset + entry.size]
                if entry.flags == 0 and kind in (0, 2, 4, 6, 11) and not entry.resource_id & 0x800000:
                    parts += len(decode_mesh(data)); meshes += 1
                if entry.flags == 1 and kind in (8, 11):
                    decode_texture(data, entry.resource_id); textures += 1
        self.assertEqual((meshes, parts, textures), (481, 1616, 160))


if __name__ == '__main__':
    unittest.main()
