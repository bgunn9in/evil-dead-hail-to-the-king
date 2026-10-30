import hashlib
import struct
import tempfile
from pathlib import Path
import unittest

from extract_dreamcast import GdiImage, SYNC, directory_records, extract


def record(name, lba=45021, size=5, directory=False):
    data = bytearray(33 + len(name) + (not len(name) % 2))
    data[0] = len(data)
    struct.pack_into('<I', data, 2, lba)
    struct.pack_into('>I', data, 6, lba)
    struct.pack_into('<I', data, 10, size)
    struct.pack_into('>I', data, 14, size)
    data[25] = 2 if directory else 0
    data[28:32] = b'\x01\0\0\x01'
    data[32] = len(name)
    data[33:33 + len(name)] = name
    return data


class DreamcastExtractionTests(unittest.TestCase):
    def fixture(self, root):
        sectors = [bytearray(2048) for _ in range(24)]
        sectors[16][:7] = b'\x01CD001\x01'
        sectors[16][40:72] = b'EVIL_DEAD'.ljust(32, b' ')
        struct.pack_into('<H', sectors[16], 128, 2048)
        sectors[16][156:190] = record(b'\0', 45020, 2048, True)
        item = record(b'HELLO.TXT;1')
        sectors[20][:len(item)] = item
        sectors[21][:5] = b'hello'
        raw = b''.join(SYNC + b'\0\0\0\x01' + s + bytes(288) for s in sectors)
        (root / 'track.bin').write_bytes(raw)
        gdi = root / 'disc.gdi'
        gdi.write_text('1\n1 45000 4 2352 track.bin 0\n', encoding='ascii')
        return gdi

    def test_absolute_lba_extract_and_provenance(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            gdi = self.fixture(root)
            result = extract(gdi, root / 'files')
            self.assertEqual((root / 'files/HELLO.TXT').read_bytes(), b'hello')
            self.assertEqual(result['files'][0]['sha256'], hashlib.sha256(b'hello').hexdigest())
            self.assertEqual(result['files'][0]['lba'], 45021)
            self.assertEqual(result['file_count'], 1)
            self.assertTrue((root / 'files-manifest.json').is_file())
            with self.assertRaises(FileExistsError):
                extract(gdi, root / 'files')

    def test_bad_sector_and_unmapped_extent(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            image = GdiImage(self.fixture(root))
            with self.assertRaises(ValueError):
                image.read(44999, 1)
            track = root / 'track.bin'
            data = bytearray(track.read_bytes())
            data[15] = 2
            track.write_bytes(data)
            with self.assertRaises(ValueError):
                image.read(45000, 1)

    def test_reject_unsafe_names_attributes_and_endian_mismatch(self):
        for data in (record(b'../BAD;1'), record(b'A/B;1')):
            with self.assertRaises(ValueError):
                list(directory_records(data))
        data = record(b'HELLO;1')
        data[6] ^= 1
        with self.assertRaises(ValueError):
            list(directory_records(data))
        data = record(b'HELLO;1')
        data[25] = 128
        with self.assertRaises(ValueError):
            list(directory_records(data))

    def test_reject_track_escape(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            gdi = self.fixture(root)
            sub = root / 'nested'
            sub.mkdir()
            bad = sub / 'disc.gdi'
            bad.write_text('1\n1 45000 4 2352 ../track.bin 0\n', encoding='ascii')
            with self.assertRaises(ValueError):
                GdiImage(bad)


if __name__ == '__main__':
    unittest.main()
