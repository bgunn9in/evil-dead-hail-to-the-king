from io import BytesIO
from pathlib import Path
import struct
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from PIL import Image
import import_dreamcast_runtime as imp
from compress_dreamcast_runtime import BASE, FORMAT, OVERLAY, bmp_shape, decode, derive, immutable_copy, kind, stored_name, verify_derivation


class LosslessStorageTests(unittest.TestCase):
    def test_depth_duplicates_remain_exact_without_exhausting_hardlink_slots(self):
        with TemporaryDirectory() as folder:
            root = Path(folder); source = root / 'base' / 'assets'; source.mkdir(parents=True)
            (source / 'provenance').mkdir(); inputs = b'{}'; (source / 'provenance/import-inputs.json').write_bytes(inputs)
            raw = struct.pack('<f', 1) * (320 * 240)
            for name in ['first.depth', 'second.depth']:(source / name).write_bytes(raw)
            rows = [{'path': p.relative_to(source).as_posix(), 'bytes': p.stat().st_size, 'sha256': imp.digest(p.read_bytes())}
                    for p in sorted(source.rglob('*')) if p.is_file()]
            base = {'format': imp.FORMAT, 'status': 'verified-import', 'source_manifest_sha256': imp.SOURCE_SHA,
                    'inputs_manifest_sha256': imp.digest(inputs), 'files': rows, 'file_count': len(rows), 'files_sha256': imp.digest(imp.json_bytes(rows))}
            encoded = imp.json_bytes(base); (source / imp.MARKER).write_bytes(encoded)
            output = root / 'compressed'; derive(source, output, imp.digest(encoded))
            a, b = output / 'assets/first.depth.png', output / 'assets/second.depth.png'
            self.assertEqual(a.read_bytes(), b.read_bytes())
            self.assertNotEqual(a.stat().st_ino, b.stat().st_ino)
            self.assertEqual((source / 'first.depth').read_bytes(), raw)
    def test_resealing_changed_pixels_cannot_fake_lossless_conversion(self):
        image = Image.new('RGB', (32, 8), (12, 34, 56))
        original = BytesIO(); image.save(original, format='BMP'); raw = original.getvalue()
        source = {'path': 'scene/background_0.bmp', 'bytes': len(raw), 'sha256': imp.digest(raw)}
        base = {'format': imp.FORMAT, 'status': 'verified-import', 'source_manifest_sha256': imp.SOURCE_SHA,
                'inputs_manifest_sha256': '0' * 64, 'files': [source], 'file_count': 1,
                'files_sha256': imp.digest(imp.json_bytes([source]))}
        base_raw = imp.json_bytes(base)
        with TemporaryDirectory() as folder:
            assets = Path(folder); (assets / 'scene').mkdir(); (assets / 'provenance').mkdir()
            (assets / BASE).write_bytes(base_raw)
            path = assets / 'scene/background_0.png'; image.save(path)
            def seal():
                stored = {'path': 'scene/background_0.png', 'bytes': path.stat().st_size, 'sha256': imp.digest(path.read_bytes())}
                change = {'source': source, 'stored': stored, 'codec': 'bmp24-png', 'header': raw[:54].hex()}
                overlay_raw = imp.json_bytes({'format': FORMAT, 'changes': [change]}); (assets / OVERLAY).write_bytes(overlay_raw)
                rows = [stored] + [{'path': name, 'bytes': len(data), 'sha256': imp.digest(data)} for name, data in ((BASE, base_raw), (OVERLAY, overlay_raw))]
                return dict(base, files=rows, file_count=3, lossless_storage={'format': FORMAT, 'base_manifest_path': BASE,
                    'base_manifest_sha256': imp.digest(base_raw), 'overlay_manifest_path': OVERLAY, 'overlay_manifest_sha256': imp.digest(overlay_raw)})
            self.assertEqual(verify_derivation(assets, seal()), base)
            Image.new('RGB', (32, 8), (1, 2, 3)).save(path)
            with self.assertRaisesRegex(ValueError, 'reconstruct original bytes'):
                verify_derivation(assets, seal())
    def test_ntfs_hardlink_limit_falls_back_without_overwriting(self):
        with TemporaryDirectory() as folder:
            source, target = Path(folder) / 'source', Path(folder) / 'target'
            source.write_bytes(b'immutable pixels')
            limit = OSError('NTFS hardlink limit'); limit.winerror = 1142
            with patch('compress_dreamcast_runtime.os.link', side_effect=limit):
                immutable_copy(source, target)
                self.assertEqual(target.read_bytes(), source.read_bytes())
                with self.assertRaises(FileExistsError):immutable_copy(source, target)
            with patch('compress_dreamcast_runtime.os.link', side_effect=PermissionError):
                with self.assertRaises(PermissionError):immutable_copy(source, Path(folder) / 'other')
    def test_original_bitmap_bytes_round_trip_not_only_visual_pixels(self):
        image = Image.new('RGB', (3, 2), (12, 34, 56))
        stream = BytesIO(); image.save(stream, format='BMP'); raw = stream.getvalue()
        offset = struct.unpack_from('<I', raw, 10)[0]
        with TemporaryDirectory() as folder:
            path = Path(folder) / 'image.png';image.save(path)
            self.assertEqual(decode(path, {'codec': 'bmp24-png', 'header': raw[:offset].hex()}), raw)
        broken = bytearray(raw[:offset]);struct.pack_into('<I', broken, 18, 50000)
        with self.assertRaises(ValueError):bmp_shape(broken)

    def test_float_bits_and_two_bit_coverage_round_trip(self):
        with TemporaryDirectory() as folder:
            for codec, mode, shape, raw in (
                ('float32-png', 'RGBA', (320, 240), struct.pack('<f', .1234567) * (320 * 240)),
                ('coverage8-png', 'L', (256, 256), bytes([0, 85, 170, 255]) * (256 * 256 // 4))):
                path = Path(folder) / (codec + '.png');Image.frombytes(mode, shape, raw).save(path)
                self.assertEqual(decode(path, {'codec': codec}), raw)
            Image.new('RGB', (256, 256)).save(path)
            with self.assertRaises(ValueError):decode(path, {'codec': 'coverage8-png'})

    def test_scope_keeps_fonts_and_sfx_original(self):
        for name in ['font.bmp', 'levelB/font.bmp', 'audio/global/0d8c7728.wav', 'gui/text.bin', 'actors/ash/actions.bin']:
            self.assertIsNone(kind(name))
        self.assertEqual(stored_name('ak01/background_7.bmp', kind('ak01/background_7.bmp')), 'ak01/background_7.png')
        self.assertEqual(stored_name('ak01/background_7.depth', kind('ak01/background_7.depth')), 'ak01/background_7.depth.png')
        self.assertEqual(kind('levelB/audio/streams/61000000.wav'), 'pcm16-flac')


if __name__ == '__main__':unittest.main()
