from pathlib import Path
import hashlib
import struct
import tempfile
import unittest

from PIL import Image
from ed_native_font import native_font
from export_dreamcast_font import RUSSIAN, export_font

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'build/dreamcast-original-20261027/files'


@unittest.skipUnless(SOURCE.is_dir(), 'Original Dreamcast source corpus')
class DreamcastFontExportTests(unittest.TestCase):
    def test_native_latin_cells_preserved_and_reviewed_russian_bytes(self):
        data = native_font(SOURCE)
        self.assertEqual(len(data['charset']), 161)
        self.assertEqual(data['charset'][88:], RUSSIAN)
        self.assertIsNone(data['provenance']['external_font'])
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / 'font'
            proof = export_font(SOURCE, output)
            self.assertEqual(proof['original_glyphs_preserved'], 88)
            self.assertEqual(hashlib.sha256((output / 'font.bmp').read_bytes()).hexdigest(), 'fffe53b1e61f7d4326cf2051c23c82e519d01595e4b35e0aad36f8e84ee7cb67')
            self.assertEqual(hashlib.sha256((output / 'font.txt').read_bytes()).hexdigest(), 'ea6086b87c27c4da641fabf03638b5fafd6ed01aa9e2eae1fd064af52ab24386')
            bmp = (output / 'font.bmp').read_bytes()
            offset, = struct.unpack_from('<I', bmp, 10)
            # Pillow's generic BMP reader discards BI_RGB's stored alpha byte.
            image = Image.frombytes('RGBA', (256, 512), bmp[offset:], 'raw', 'BGRA', 0, -1)
            self.assertEqual(image.size, (256, 512))
            for n in range(88):
                box = (n % 14 * 18, n // 14 * 24, n % 14 * 18 + 18, n // 14 * 24 + 24)
                self.assertEqual(image.crop(box).tobytes(), data['original'].crop(box).tobytes())
            self.assertEqual((output / 'font.txt').read_bytes().splitlines()[:3], [b'EDFONT2', b'18 24 14 256 512', b'CP1251'])
            with self.assertRaises(ValueError):export_font(SOURCE, output)
            with self.assertRaises(ValueError):export_font(SOURCE, SOURCE / 'nested-font')

    def test_all_added_strokes_are_nonempty_and_native_coverage(self):
        data = native_font(SOURCE)
        for letter in RUSSIAN:
            self.assertTrue(data['glyphs'][letter].getbbox(), letter)
            self.assertTrue(all(v % 17 == 0 for v in data['glyphs'][letter].tobytes()), letter)
        for russian, latin in zip('АВЕКМНОРСТХаеорсух', 'ABEKMHOPCTXaeopcyx'):
            self.assertEqual(data['glyphs'][russian].tobytes(), data['glyphs'][latin].tobytes())

    def test_cyrillic_shapes_keep_native_serifs_and_lowercase_metrics(self):
        glyph = native_font(SOURCE)['glyphs']
        # These are letters with diagonals, not an A crossbar or an added underline.
        for letter in 'ЛД':
            self.assertFalse(glyph[letter].crop((6, 12, 11, 15)).getbbox(), letter)
        self.assertEqual(glyph['Л'].getpixel((8, 17)), 0)
        for letter in 'вгжзиклмнптчшъыьэюя':
            self.assertEqual(glyph[letter].getbbox()[1::2], (7, 19), letter)
        self.assertEqual(glyph['б'].getbbox()[1::2], (3, 19))
        for letter in 'дцщф':
            self.assertEqual(glyph[letter].getbbox()[3], 23, letter)
        self.assertEqual(glyph['н'].crop((5, 11, 13, 14)).tobytes(),
                         glyph['H'].crop((5, 10, 13, 13)).tobytes())

    def test_unknown_table_and_source_version_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'source'
            source.mkdir()
            for name in ('FONT5.PVR', '1ST_READ.BIN'):
                (source / name).write_bytes((SOURCE / name).read_bytes())
            executable = bytearray((source / '1ST_READ.BIN').read_bytes())
            executable[715000] = ord('?')
            (source / '1ST_READ.BIN').write_bytes(executable)
            with self.assertRaises(ValueError):native_font(source)
            (source / '1ST_READ.BIN').write_bytes((SOURCE / '1ST_READ.BIN').read_bytes())
            raw = bytearray((source / 'FONT5.PVR').read_bytes())
            raw[-1] ^= 1
            (source / 'FONT5.PVR').write_bytes(raw)
            with self.assertRaises(ValueError):native_font(source)


if __name__ == '__main__':
    unittest.main()
