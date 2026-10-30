from io import BytesIO
from pathlib import Path
import struct
import tempfile
import unittest

from PIL import Image

from ed_assets import DATA_OFFSET, DC_DATA_OFFSET, decode_background, extract_archive, parse_archive, parse_dc_archive


def make_archive(payloads):
    data = bytearray(b"\xff" * DATA_OFFSET)
    struct.pack_into("<6I", data, 0, 0x61610101, 0x30002, 0, 20, 0, len(payloads))
    for index, payload in enumerate(payloads):
        struct.pack_into("<4I", data, 24 + 16 * index, 0x08800800, 0, len(data), len(payload))
        data.extend(payload)
    return data


class ArchiveTests(unittest.TestCase):
    def test_duplicate_ids_and_unreliable_header_length(self):
        data = make_archive([b"first", b"second"])
        entries = parse_archive(data)
        self.assertEqual(len(entries), 2)
        self.assertEqual(entries[0].resource_id, entries[1].resource_id)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            extract_archive(data, entries, output)
            self.assertEqual((output / "0000_08800800.bin").read_bytes(), b"first")
            self.assertEqual((output / "0001_08800800.bin").read_bytes(), b"second")

    def test_invalid_table_and_payload_boundaries(self):
        mutations = [(20, 1001), (32, DATA_OFFSET - 1), (36, 100), (12, 24), (4, 0)]
        for offset, value in mutations:
            with self.subTest(offset=offset):
                data = make_archive([b"test"])
                struct.pack_into("<I", data, offset, value)
                with self.assertRaises(ValueError):
                    parse_archive(data)
        with self.assertRaises(ValueError):
            parse_archive(b"short")
        with self.assertRaises(ValueError):
            parse_archive(make_archive([b"test"]) + b"unindexed")

    def test_existing_output_prevents_partial_extraction(self):
        data = make_archive([b"first", b"second"])
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            existing = output / "0001_08800800.bin"
            existing.write_bytes(b"keep")
            with self.assertRaises(FileExistsError):
                extract_archive(data, parse_archive(data), output)
            self.assertEqual(existing.read_bytes(), b"keep")
            self.assertFalse((output / "0000_08800800.bin").exists())


class DreamcastArchiveTests(unittest.TestCase):
    def archive(self):
        data=bytearray(DC_DATA_OFFSET)
        struct.pack_into("<6I",data,0,0x61610100,0x30004,0,20,0,2)
        for index,payload in enumerate((b"abcd",b"efgh")):
            struct.pack_into("<4I",data,24+16*index,0x31800800,0,len(data),len(payload))
            data.extend(payload)
        return data

    def test_explicit_layout_preserves_duplicate_ids_and_pc_default(self):
        data=self.archive();entries=parse_dc_archive(data)
        self.assertEqual([(e.offset,e.size) for e in entries],[(0x4000,4),(0x4004,4)])
        self.assertEqual(entries[0].resource_id,entries[1].resource_id)
        with self.assertRaises(ValueError):parse_archive(data)
        with self.assertRaises(ValueError):parse_dc_archive(make_archive([b"test"]))

    def test_bounds_padding_and_unindexed_tail_fail_closed(self):
        for offset,value in ((20,1001),(32,DC_DATA_OFFSET-4),(36,0xffffffff),(48,DC_DATA_OFFSET),(12,24),(4,0)):
            with self.subTest(offset=offset):
                data=self.archive();struct.pack_into("<I",data,offset,value)
                with self.assertRaises(ValueError):parse_dc_archive(data)
        data=self.archive();data[DATA_OFFSET]=1
        with self.assertRaises(ValueError):parse_dc_archive(data)
        with self.assertRaises(ValueError):parse_dc_archive(self.archive()+b"tail")
        with self.assertRaises(ValueError):parse_dc_archive(self.archive()[:-1])


class BackgroundTests(unittest.TestCase):
    def setUp(self):
        self.data = bytearray(b"scene.jpg\0".ljust(128, b"\0"))
        self.colors = []
        for index in range(15):
            color = (index * 13, index * 7, index * 3)
            self.colors.append(color)
            stream = BytesIO()
            Image.new("RGB", (128, 128), color).save(stream, format="JPEG", quality=100)
            payload = stream.getvalue()
            payload += b"\0" * (-len(payload) % 4)
            self.data.extend(struct.pack("<I", len(payload)))
            self.data.extend(payload)

    def test_tile_orientation_and_mask_tail(self):
        tail = b"\x02\0\0\0unparsed mask data"
        image, source_name, tail_offset = decode_background(self.data + tail)
        self.assertEqual(image.size, (640, 384))
        self.assertEqual(source_name, "scene.jpg")
        self.assertEqual(bytes((self.data + tail)[tail_offset:]), tail)
        for index, expected in enumerate(self.colors):
            actual = image.getpixel(((index % 5) * 128 + 64, (2 - index // 5) * 128 + 64))
            self.assertTrue(all(abs(a - e) <= 2 for a, e in zip(actual, expected)))

    def test_truncated_tile_and_invalid_length(self):
        with self.assertRaises(ValueError):
            decode_background(self.data[:-10])
        struct.pack_into("<I", self.data, 128, 0xFFFFFFFF)
        with self.assertRaises(ValueError):
            decode_background(self.data)

    def test_wrong_tile_dimensions(self):
        stream = BytesIO()
        Image.new("RGB", (64, 128)).save(stream, format="JPEG")
        payload = stream.getvalue()
        payload += b"\0" * (-len(payload) % 4)
        data = b"scene.jpg\0".ljust(128, b"\0") + struct.pack("<I", len(payload)) + payload
        with self.assertRaises(ValueError):
            decode_background(data)


if __name__ == "__main__":
    unittest.main()
