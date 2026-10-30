from pathlib import Path
import shutil
import tempfile
import unittest

from export_dreamcast_visual import convert_frame, export_profile, read_pgmyuv


class DcVisualTests(unittest.TestCase):
    def test_pgmyuv_planes_are_complete_and_fixed_size(self):
        raw = b'P5\n320 720\n255\n' + bytes(320 * 240 * 3)
        self.assertEqual(len(read_pgmyuv(raw)), 320 * 240 * 3)
        for data in (b'', raw[:-1], raw + b'\0', raw.replace(b'720', b'240', 1)):
            with self.assertRaises(ValueError):
                read_pgmyuv(data)

    @unittest.skipUnless(shutil.which('ffmpeg'), 'Existing FFmpeg color converter is optional')
    def test_full_range_yuvj_is_not_gray_or_studio_range(self):
        frame = b'P5\n320 720\n255\n' + bytes([16]) * (320 * 240) + bytes([128]) * (320 * 240 * 2)
        image = convert_frame(frame, shutil.which('ffmpeg'))
        self.assertEqual(image.size, (320, 240))
        self.assertEqual(image.getpixel((100, 100)), (16, 16, 16, 255))

    def test_existing_output_and_original_paths_fail_before_writes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, decoded = root / 'original', root / 'decoded'
            source.mkdir(); decoded.mkdir()
            for output in (source, source / 'child', decoded, decoded / 'child'):
                with self.assertRaises(ValueError):
                    export_profile(source, decoded, output, 'AA01', 'unused')
            with self.assertRaises(FileExistsError):
                export_profile(source, decoded, root, 'AA01', 'unused')
            with self.assertRaises(ValueError):
                export_profile(source, decoded, root / 'new', 'UNKNOWN', 'unused')
            self.assertEqual(list(source.iterdir()), [])
            self.assertEqual(list(decoded.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
