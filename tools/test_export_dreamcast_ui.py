import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from export_dreamcast_ui import Resources, debug_union_overlay, export_ui, pack_uv, read_gui, read_inventory, text_payload


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'build/dreamcast-original-20261027/files'


def archive(rows):
    data = bytearray(0x4000)
    struct.pack_into('<6I', data, 0, 0x61610100, 0x30004, 0, 20, 0, len(rows))
    for index, (identity, flags, raw) in enumerate(rows):
        struct.pack_into('<4I', data, 24 + index * 16, identity, flags, len(data), len(raw))
        data += raw
    return data


class DreamcastUiTests(unittest.TestCase):
    def test_exact_resource_priority_flags_and_conflict(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'AZ01.DC').write_bytes(archive([(11, 0, b'first'), (11, 1, b'tex')]))
            (root / 'BZ01.DC').write_bytes(archive([(11, 0, b'first'), (12, 0, b'fallback')]))
            data = Resources(root)
            raw, record = data.get(('AZ01.DC', 'BZ01.DC'), 11, 0)
            self.assertEqual(raw, b'first')
            self.assertEqual(record['archive'], 'AZ01.DC')
            self.assertEqual(len(record['identical_alternatives']), 1)
            self.assertEqual(data.get(('AZ01.DC',), 11, 1)[0], b'tex')
            self.assertEqual(data.get(('AZ01.DC', 'BZ01.DC'), 12, 0)[0], b'fallback')
            with self.assertRaises(ValueError):data.get(('AZ01.DC',), 12, 0)
            (root / 'BZ01.DC').write_bytes(archive([(11, 0, b'other')]))
            with self.assertRaises(ValueError):Resources(root).get(('AZ01.DC', 'BZ01.DC'), 11, 0)

    def test_ambiguous_requested_resource_rejects_without_banning_unrelated_sound_ids(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'AZ01.DC').write_bytes(archive([(1, 1, b'a'), (1, 1, b'b'), (2, 0, b'c')]))
            data = Resources(root)
            self.assertEqual(data.get(('AZ01.DC',), 2, 0)[0], b'c')
            with self.assertRaises(ValueError):data.get(('AZ01.DC',), 1, 1)

    def test_dc_text_padding_is_checked_not_silently_trimmed(self):
        self.assertEqual(text_payload(b'Hello\n\0\0\0'), b'Hello\n\0')
        for bad in (b'Hello', b'Hi\0tail', b'\xff\0', b'x' * 32767 + b'\0'):
            with self.subTest(bad=bad[:10]), self.assertRaises(ValueError):text_payload(bad)

    def test_uv_keeps_unexpanded_ordinals_and_rejects_bad_indices(self):
        models = {0x0b142038: [{'uv': [[.1, .2], [.3, .4]], 'triangles': [[1, 0, 1]]}]}
        raw = pack_uv(models)
        self.assertEqual(raw[:8], b'EGUIUV1\0')
        self.assertEqual(struct.unpack_from('<2I', raw, 20), (2, 3))
        self.assertEqual(struct.unpack_from('<3H', raw, 44), (1, 0, 1))
        for part in ({'uv': [[float('nan'), 0]], 'triangles': []}, {'uv': [[0, 0]], 'triangles': [[0, 1, 0]]}):
            with self.assertRaises(ValueError):pack_uv({0x0b142038: [part]})

    def test_output_guard_preserves_existing_and_originals(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = root / 'original'
            original.mkdir()
            keep = original / 'keep'
            keep.write_bytes(b'original')
            for target in (original, root, original / 'child'):
                with self.assertRaises(ValueError):export_ui(original, target)
            self.assertEqual(keep.read_bytes(), b'original')
            self.assertFalse((original / 'child').exists())
            with patch('ed_dc_audio.ROOT', root):
                with self.assertRaises(ValueError):export_ui(original, root / 'game' / 'ui-export')
            self.assertFalse((root / 'game').exists())

    @unittest.skipUnless(SOURCE.exists(), 'Local original Dreamcast corpus')
    def test_actual_a_b_geometry_uv_and_inventory_queries(self):
        resources = Resources(SOURCE)
        a, b = read_gui(resources, 'A'), read_gui(resources, 'B')
        self.assertEqual((len(a['models']), sum(map(len, a['models'].values())), len(a['textures']), len(a['texts'])), (95, 624, 8, 443))
        self.assertEqual((len(b['models']), sum(map(len, b['models'].values())), len(b['textures']), len(b['texts'])), (102, 647, 9, 392))
        self.assertEqual(hashlib.sha256(pack_uv(a['models'])).hexdigest(), '510ee45ab2d9c2fdde78fc7f6501db3a6f304cdcb5261c8745180394325f8111')
        self.assertEqual(len(read_inventory(resources, 'A')['items']), 87)
        self.assertEqual(len(read_inventory(resources, 'B')['items']), 121)
        union = read_inventory(resources, 'A', True)
        self.assertEqual(len(union['items']), 121)
        self.assertEqual(len(union['textures']), 3)
        extra, bridge = debug_union_overlay(a, union)
        self.assertEqual((bridge['models'], bridge['textures'], bridge['inventory_items_covered']), (96, 9, 121))
        self.assertEqual(set(extra), {'gui/all-models/0b142248.mesh', 'gui/textures/0b942248.rgba', 'gui/layout.bin', 'gui/uv-ordinals.bin'})
        self.assertEqual(debug_union_overlay(b, union)[0], {})
        for identity, (_, metadata) in a['textures'].items():
            self.assertEqual(len(a['originals'][f'{identity:08x}-1.resource']), 16 + metadata['pvrt_bytes'] + metadata['tail_bytes'])


if __name__ == '__main__':
    unittest.main()
