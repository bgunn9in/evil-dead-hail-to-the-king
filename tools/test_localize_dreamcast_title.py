from pathlib import Path
import tempfile
import unittest

from ed_native_font import native_font

from ed_dc_model import decode_mesh, decode_texture
from export_dreamcast_ui import Resources
from localize_dreamcast_title import TEXTURE, localize_title, recolor_title, regions


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'build/dreamcast-original-20261027/files'


class RussianTitleTests(unittest.TestCase):
    @unittest.skipUnless(SOURCE.exists(), 'Original Dreamcast source corpus')
    def test_actual_uv_regions_hero_dots_protected_and_export(self):
        resources = Resources(SOURCE)
        raw, _ = resources.get(('AZ01.DC', 'BZ01.DC'), TEXTURE, 1)
        model, _ = resources.get(('AZ01.DC', 'BZ01.DC'), 0x0b142088, 0)
        image = decode_texture(raw, TEXTURE)[0]
        parts = decode_mesh(model)
        translated, proof = recolor_title(image, parts, native_font(SOURCE))
        self.assertEqual(proof['outside_region_changes'], 0)
        self.assertEqual(regions(parts, image.size)[-2]['box'], (105, 124, 126, 256))
        self.assertEqual(regions(parts, image.size)[-1]['box'], (80, 124, 101, 229))
        self.assertEqual(image.crop((0, 0, 64, 256)).tobytes(), translated.crop((0, 0, 64, 256)).tobytes())
        self.assertEqual(image.crop((80, 229, 102, 240)).tobytes(), translated.crop((80, 229, 102, 240)).tobytes())
        for row in proof['regions']:
            self.assertNotEqual(image.crop(row['box']).tobytes(), translated.crop(row['box']).tobytes())
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'new'
            localize_title(SOURCE, output)
            payload = (output / 'A/gui/textures/0b942034.rgba').read_bytes()
            self.assertEqual(__import__('hashlib').sha256(payload).hexdigest(), '0e03a7a83ac729e105f42952233547fa36afb3d776379a10737ffac5e61df9e4')
            for relative in ('B/gui/textures/0b942034.rgba', 'A/aa01/main-menu.texture', 'B/bb01/main-menu.texture'):
                self.assertEqual(payload, (output / relative).read_bytes())
            with self.assertRaises(ValueError):localize_title(SOURCE, output)

    def test_reject_unknown_model_size_uv_and_overlap(self):
        parts = [{'uv': [[.83, .49], [.98, .99]]}, {'uv': [[.63, .49], [.78, .89]]}] + [{}] * 6
        self.assertEqual(len(regions(parts, (128, 256))), 10)
        with self.assertRaises(ValueError):regions(parts, (256, 256))
        with self.assertRaises(ValueError):regions(parts[:2], (128, 256))
        parts[0] = {'uv': [[float('nan'), 0]]}
        with self.assertRaises(ValueError):regions(parts, (128, 256))
        parts[0] = {'uv': [[.5, .01], [.99, .99]]}
        with self.assertRaises(ValueError):regions(parts, (128, 256))


if __name__ == '__main__':
    unittest.main()
