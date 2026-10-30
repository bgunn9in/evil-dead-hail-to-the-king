import hashlib
import json
import math
from pathlib import Path
import struct
import tempfile
import unittest

from ed_assets import parse_dc_archive
from ed_dc_masks import (ARGB4444_COVERAGE, DISTANCE_EPSILON, decode_dc_masks,
                         export_dc_masks, load_dc_masks, metadata, unpack_coverage)


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "build/dreamcast-original-20261027/files"


def make_layer(pitch=1, height=1, x=0, y=0, distance=4.0, raw=None):
    size = (pitch or 256) * (height or 256)
    if raw is None:
        raw = bytes(size)
    packed = x | (y << 8) | (pitch << 16) | (height << 24)
    return struct.pack("<IIfI", 1, packed, distance, 16) + raw


class DreamcastMaskTests(unittest.TestCase):
    def test_msb_pairs_and_four_distinct_argb4444_alpha_levels(self):
        self.assertEqual(unpack_coverage(b"\x1b\xe4"), bytes((0, 85, 170, 255, 255, 170, 85, 0)))
        self.assertEqual(ARGB4444_COVERAGE, (0x0fff, 0x5fff, 0xafff, 0xffff))
        layer = decode_dc_masks(make_layer(raw=b"\x1b"))[0]
        self.assertEqual(layer["native_texture_coverage"][:4], bytes((0, 85, 170, 255)))
        self.assertEqual(layer["coverage_histogram"], {"0": 1, "85": 1, "170": 1, "255": 1})
        self.assertEqual(len(layer["native_texture_coverage"]), 256 * 256)

    def test_source_rectangle_uv_and_float_inverse_depth(self):
        layer = decode_dc_masks(make_layer(25, 112, 19, 104, 4.365900039672852))[0]
        self.assertEqual(layer["screen_rect_xywh"], [190, 195, 250, 210])
        self.assertEqual(layer["local_uv_bounds"], [0, 0, 25 / 64, 112 / 256])
        self.assertEqual(layer["background_uv_bounds"], [190 / 1024, 195 / 512, 440 / 1024, 405 / 512])
        self.assertEqual(layer["native_inverse_z"], 0.22904784977436066)
        self.assertEqual(layer["distance_bits"], struct.unpack("<I", struct.pack("<f", 4.365900039672852))[0])
        self.assertEqual(decode_dc_masks(make_layer(distance=4))[0]["native_inverse_z"], 0.25)
        self.assertEqual(decode_dc_masks(make_layer(distance=8))[0]["native_inverse_z"], 0.125)
        # Native loader changes the float before rendering; no PC projection/power patch.
        for value in (0.0, -0.0, -3.0, DISTANCE_EPSILON / 2):
            clamped = decode_dc_masks(make_layer(distance=value))[0]
            self.assertEqual(clamped["effective_distance"], 1)
            self.assertEqual(clamped["native_inverse_z"], 1)
        threshold = decode_dc_masks(make_layer(distance=DISTANCE_EPSILON))[0]
        self.assertEqual(threshold["effective_distance"], DISTANCE_EPSILON)

    def test_overscan_uses_native_row_stride_and_preserves_full_source(self):
        # First row's four excess texels are overwritten by the following row.
        raw = bytes([255] * 65 + [0] * 65)
        layer = decode_dc_masks(make_layer(65, 2, raw=raw))[0]
        self.assertEqual(layer["width"], 260)
        self.assertEqual(layer["row_overlap_texels"], 4)
        self.assertEqual(layer["raw"], raw)
        self.assertEqual(layer["coverage"][:260], bytes([255] * 260))
        native = layer["native_texture_coverage"]
        self.assertEqual(native[:256], bytes([255] * 256))
        self.assertEqual(native[256:516], bytes(260))
        self.assertEqual(layer["local_uv_bounds"][2], 65 / 64)
        self.assertEqual(layer["native_write_spill"], b"")
        # Last source row writes beyond the 256x256 texture. Retain the bytes as
        # evidence, while keeping every decoded texture output in bounds.
        raw = bytearray(65 * 256)
        raw[-1] = 0xe4
        layer = decode_dc_masks(make_layer(65, 0, raw=raw))[0]
        self.assertEqual(layer["height"], 256)
        self.assertEqual(layer["native_write_spill"], bytes((255, 170, 85, 0)))
        self.assertEqual(len(layer["native_texture_coverage"]), 65536)
        self.assertEqual(layer["coverage"][-4:], bytes((255, 170, 85, 0)))

    def test_zero_pitch_native_fallback_is_not_reinterpreted_as_64(self):
        # No zero-pitch row occurs in the packaged corpus. The source routine
        # nevertheless substitutes 256 bytes, not a 256-texel width.
        layer = decode_dc_masks(make_layer(0, 1))[0]
        self.assertEqual(layer["pitch_bytes"], 256)
        self.assertEqual(layer["width"], 1024)
        self.assertEqual(layer["row_overlap_texels"], 768)
        self.assertEqual(layer["local_uv_bounds"][2], 4)

    def test_invalid_tables_ranges_and_distances_fail_closed(self):
        valid = make_layer(raw=b"\x1b")
        bad = [b"", b"\x01\0\0", struct.pack("<I", 4097), valid[:15], valid[:-1]]
        for offset in (0, 4, 15, 17, 0xffffffff):
            changed = bytearray(valid)
            struct.pack_into("<I", changed, 12, offset)
            bad.append(changed)
        for distance in (math.nan, math.inf, -math.inf):
            bad.append(make_layer(distance=distance))
        for data in bad:
            with self.subTest(data=bytes(data[:16]).hex()):
                with self.assertRaises(ValueError):
                    decode_dc_masks(data)
        self.assertEqual(decode_dc_masks(struct.pack("<I", 0)), [])
        # Bounded aliasing is retained: the directory may reference the same
        # original bitmap, and source order still matters to the renderer.
        alias = struct.pack("<IIfIIfI", 2, 0x01010000, 4, 28, 0x01010000, 8, 28) + b"\x1b"
        self.assertEqual([v["raw"] for v in decode_dc_masks(alias)], [b"\x1b", b"\x1b"])

    def test_actual_ak01_camera2_positive_oracle_and_export(self):
        archive = SOURCE / "AK01V.DC"
        if not archive.is_file():
            self.skipTest("Requires the user's extracted original Dreamcast files")
        resource = load_dc_masks(archive, 35)
        self.assertEqual(resource["sha256"], "c6d0b070a4a8a7e1a48677de58957f795605ff2ab644edd193b22197d1209f30")
        self.assertEqual((resource["id"], resource["offset"], resource["bytes"]), (0x088a0840, 162624, 11356))
        layers = resource["layers"]
        self.assertEqual([v["packed_rect"] for v in layers], [0xd0290000, 0x70196813])
        self.assertEqual([v["screen_rect_xywh"] for v in layers], [[0, 0, 410, 390], [190, 195, 250, 210]])
        self.assertEqual([v["sha256"] for v in layers], [
            "cd5ebacd0fd2a4f564faa6d1c8df576bb2a5d43bd6862e33b123303456606125",
            "0a294c864dfa4f29c37318bfba62cfb8f79cdbeee094dcd749875ee2744a68cf"])
        self.assertEqual(layers[0]["coverage_histogram"], {"0": 11671, "85": 75, "170": 72, "255": 22294})
        self.assertEqual(layers[1]["coverage_histogram"], {"0": 8104, "85": 68, "170": 60, "255": 2968})
        self.assertEqual(layers[0]["native_inverse_z"], 0.2282688170671463)
        before = hashlib.sha256(archive.read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "new-mask-inspection"
            export_dc_masks(archive, 35, output)
            self.assertEqual(json.loads((output / "source.json").read_text()), metadata(resource))
            self.assertEqual((output / "layer001.coverage8").read_bytes(), layers[1]["coverage"])
            with self.assertRaises(FileExistsError):
                export_dc_masks(archive, 35, output)
            with self.assertRaises(ValueError):
                export_dc_masks(archive, 35, archive.parent)
        self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(), before)
        for index in (-1, 1000, 0):
            with self.assertRaises(ValueError):
                load_dc_masks(archive, index)

    def test_every_original_foreground_keeps_all_570_layers_including_pitch65(self):
        if not SOURCE.is_dir():
            self.skipTest("Requires the user's extracted original Dreamcast files")
        backgrounds = empty = layers = overscan = zero_height = 0
        for path in sorted(SOURCE.glob("*V.DC")):
            archive = path.read_bytes()
            for entry in parse_dc_archive(archive):
                if entry.flags != 0 or entry.resource_id >> 24 != 8:
                    continue
                payload = archive[entry.offset:entry.offset + entry.size]
                decoded = decode_dc_masks(payload)
                backgrounds += 1
                empty += not decoded
                layers += len(decoded)
                for layer in decoded:
                    overscan += layer["pitch_bytes"] == 65
                    zero_height += layer["height_byte"] == 0
                    self.assertEqual(layer["raw"], payload[layer["offset"]:layer["offset"] + layer["bytes"]])
                    self.assertEqual(len(layer["coverage"]), layer["width"] * layer["height"])
                    self.assertEqual(len(layer["native_texture_coverage"]), 65536)
                    self.assertTrue(math.isfinite(layer["native_inverse_z"]))
        self.assertEqual((backgrounds, empty, layers, overscan, zero_height), (4330, 4041, 570, 38, 56))


if __name__ == "__main__":
    unittest.main()
