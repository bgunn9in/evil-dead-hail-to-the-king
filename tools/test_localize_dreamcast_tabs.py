from pathlib import Path
import tempfile
import unittest

from PIL import Image
from ed_native_font import native_font

from ed_dc_model import decode_mesh, decode_texture
from export_dreamcast_ui import Resources
from localize_dreamcast_tabs import MODEL, TEXTURE, LABELS, cells, glyph_mask, localize_tabs, paper, recolor_tabs


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'build/dreamcast-original-20261027/files'


class RussianTabsTests(unittest.TestCase):
    def test_cell_bounds_overlap_and_nonfinite_reject(self):
        parts = [{'uv': [[(n * 2 + 1) / 100, .1], [(n * 2 + 2) / 100, .2]]} for n in range(14)]
        self.assertEqual(len(cells(parts, (1000, 1000))), 14)
        with self.assertRaises(ValueError):cells(parts[:-1], (1000, 1000))
        parts[1] = parts[0]
        with self.assertRaises(ValueError):cells(parts, (1000, 1000))
        parts[0] = {'uv': [[float('nan'), 0]]}
        with self.assertRaises(ValueError):cells(parts, (1000, 1000))

    def test_paper_reconstruction_preserves_alpha_and_only_cell(self):
        original = Image.new('RGBA', (20, 20), (120, 100, 80, 40))
        original.putpixel((5, 5), (0, 0, 0, 19))
        patch = paper(original, (4, 4, 10, 10), False)
        self.assertEqual(patch.getpixel((1, 1)), (120, 100, 80, 19))
        self.assertEqual(original.getpixel((5, 5)), (0, 0, 0, 19))

    def test_glyph_mask_is_bounded_and_coverage_has_source_steps(self):
        font = native_font(SOURCE)
        mask = glyph_mask('TEXT', font, (50, 18))
        self.assertTrue(mask.getbbox())
        self.assertTrue(all(v % 17 == 0 for v in mask.tobytes()))
        self.assertEqual(mask.crop((0, 0, 1, 18)).getbbox(), None)
        with self.assertRaises(ValueError):glyph_mask('TEXT', font, (2, 2))

    @unittest.skipUnless(SOURCE.exists(), 'Original Dreamcast source corpus')
    def test_actual_a_b_identical_source_and_all_states_protected(self):
        resources = Resources(SOURCE)
        raw, _ = resources.get(('AZ01.DC', 'BZ01.DC'), TEXTURE, 1)
        model, _ = resources.get(('AZ01.DC', 'BZ01.DC'), MODEL, 0)
        original = decode_texture(raw, TEXTURE)[0]
        translated, proof = recolor_tabs(original, decode_mesh(model), native_font(SOURCE))
        self.assertEqual(proof['outside_cell_changes'], 0)
        self.assertEqual(translated.getchannel('A').tobytes(), original.getchannel('A').tobytes())
        self.assertEqual({row['label'] for row in proof['cells']}, set(LABELS))
        self.assertEqual([row['part'] for row in proof['cells'] if row['vertical']], [3, 9])
        self.assertEqual([row['part'] for row in proof['cells'] if row['state'] == 'disabled'], [12, 13])
        for row in proof['cells']:
            self.assertNotEqual(original.crop(row['box']).tobytes(), translated.crop(row['box']).tobytes())
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / 'new'
            record = localize_tabs(SOURCE, out)
            self.assertEqual(record['rgba_sha256'], __import__('hashlib').sha256(translated.tobytes()).hexdigest())
            self.assertTrue((out / 'gui/textures/0b942054.rgba').exists())
            self.assertEqual(record['encoded_sha256'], '423d7af7602c59689133bdda33e5f620cd673da599194e65ebc8aa87622485c1')
            self.assertIsNone(record['native_font']['external_font'])
            with self.assertRaises(ValueError):localize_tabs(SOURCE, out)


if __name__ == '__main__':
    unittest.main()
