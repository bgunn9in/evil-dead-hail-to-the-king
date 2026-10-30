import math
from pathlib import Path
import struct
import unittest

from PIL import Image

from ed_masks import (AK01, AK09, SCENE_DEPTH_OVERRIDES, DepthPatches,
                      decode_masks, depth_map, mask_clip_depth, read_depth_patches)


def make_masks():
    data = bytearray(4 + 2 * 364 + 2 * 2048)
    struct.pack_into("<I", data, 0, 2)
    for layer in range(2):
        offset = 4 + layer * 364
        struct.pack_into("<f", data, offset, 5 + layer)
        for tile in range(15):
            struct.pack_into("<I", data, offset + 4 + tile * 24 + 16, 0xFFFFFFFF)
    first = 4 + 2 * 364
    struct.pack_into("<I", data, 4 + 4 + 16, first)
    struct.pack_into("<I", data, 4 + 364 + 4 + 14 * 24 + 16, first + 2048)
    data[first] = 0b10000001
    data[first + 16] = 0b00000010
    data[first + 2048] = 1
    return data


class MaskTests(unittest.TestCase):
    def test_bounded_original_patch_instruction_subset(self):
        # Synthetic x86 branch fixture, not an embedded original EXE dump.
        code = bytearray(b"\x81\xf9" + struct.pack("<I", 0x616C0300))
        branches = []
        for comparison in (b"", b"\x83\xf8\x07", b"\x85\xf6"):
            code.extend(comparison)
            branches.append(len(code))
            code.extend(b"\x0f\x85\0\0\0\0")
        code.extend(b"\xdd\xd8\xd9\x05" + struct.pack("<I", 0x500000))
        for offset in branches:
            struct.pack_into("<i", code, offset + 2, len(code) - offset - 6)
        patches = DepthPatches(code, lambda address: 0.875 if address == 0x500000 else math.nan)
        self.assertEqual(patches.lookup(0x616C0300, 7, 0)["clip_depth"], 0.875)
        for args in ((AK01, 7, 0), (0x616C0300, 8, 0), (0x616C0300, 7, 1)):
            self.assertIsNone(patches.lookup(*args))
        self.assertEqual(mask_clip_depth(5, scene_id=0x616C0300, background_index=7,
                                         layer_index=0, depth_patches=patches), 0.875)
        self.assertEqual(mask_clip_depth(0, scene_id=0x616C0300, background_index=7,
                                         layer_index=0, depth_patches=patches), 0.875)
        self.assertEqual(mask_clip_depth(0, scene_id=0x616C0300, background_index=8,
                                         layer_index=0, depth_patches=patches), math.inf)
        with self.assertRaises(ValueError):
            mask_clip_depth(-1, scene_id=0x616C0300, background_index=8,
                            layer_index=0, depth_patches=patches)
        for bad in (b"\x90", b"\xeb\x7f"):
            with self.assertRaises(ValueError):
                DepthPatches(bad, lambda _: 0.5)
        with self.assertRaisesRegex(ValueError, "budget"):
            DepthPatches(b"\xeb\xfe", lambda _: 0.5).lookup(AK01, 0, 0)
        with self.assertRaises(ValueError):
            patches.lookup(AK01, -1, 0)

    @unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
    def test_complete_original_exe_patch_tree(self):
        executable = Path(__file__).resolve().parents[1] / "game/EvilDead.exe"
        if not executable.is_file():
            self.skipTest("Requires the user's original executable")
        data = executable.read_bytes()
        patches = read_depth_patches(data)
        self.assertEqual(len(patches.instructions), 1108)
        for scene, overrides in SCENE_DEPTH_OVERRIDES.items():
            for (background, layer), expected in overrides.items():
                self.assertEqual(patches.lookup(scene, background, layer)["clip_depth"], expected)
        self.assertEqual(patches.lookup(0x616C0300, 1, 0)["clip_depth"], 0.9946359992027283)
        self.assertEqual(patches.lookup(0x61680100, 136, 0)["clip_depth"], 0.984607994556427)
        self.assertEqual(mask_clip_depth(0, scene_id=0x616C0200, background_index=54,
                                        layer_index=0, depth_patches=patches), 0.9916340112686157)
        for scene in (0x616C0100, 0x61683200, 0x61683300, 0x61680200):
            self.assertIsNone(patches.lookup(scene, 0, 0))
        changed = bytearray(data)
        changed[0x2BC66] ^= 1
        with self.assertRaisesRegex(ValueError, "verified PC version"):
            read_depth_patches(changed)
        with self.assertRaises(ValueError):
            read_depth_patches(data[:100])

    def test_lsb_first_rows_and_bottom_up_tile_layout(self):
        layers = decode_masks(make_masks(), 0, scene_id=AK01, background_index=30)
        self.assertEqual(len(layers), 2)
        first = layers[0]["mask"]
        self.assertEqual(first.getpixel((0, 256)), 255)
        self.assertEqual(first.getpixel((7, 256)), 255)
        self.assertEqual(first.getpixel((1, 257)), 255)
        self.assertEqual(first.getpixel((1, 256)), 0)
        self.assertEqual(first.getpixel((0, 383)), 0)
        self.assertEqual(layers[1]["mask"].getpixel((512, 0)), 255)
        self.assertEqual(sum(value > 0 for value in first.tobytes()), 3)

    def test_depth_conversion_and_original_override(self):
        near = 0.05000000074505806
        expected = ((1 - near / 5) * 625 / (625 - near)) ** 0.8799999952316284
        kwargs = dict(scene_id=AK01, background_index=30, layer_index=0)
        self.assertAlmostEqual(mask_clip_depth(5, **kwargs), expected)
        self.assertLess(mask_clip_depth(4, **kwargs), mask_clip_depth(6, **kwargs))
        self.assertEqual(mask_clip_depth(5.1021, scene_id=AK01, background_index=15, layer_index=1),
                         0.9887149930000305)
        self.assertEqual(mask_clip_depth(10, scene_id=AK09, background_index=19, layer_index=0),
                         0.9981769919395447)
        self.assertEqual(mask_clip_depth(10, scene_id=AK09, background_index=76, layer_index=2),
                         0.9963070154190063)

    def test_nearest_layer_and_uncovered_depth(self):
        a = Image.new("L", (640, 384))
        b = a.copy()
        a.putpixel((0, 0), 255)
        b.putpixel((0, 0), 255)
        b.putpixel((1, 0), 255)
        result = depth_map([{"mask": a, "clip_depth": 0.8}, {"mask": b, "clip_depth": 0.9}])
        self.assertEqual(len(result), 640 * 384 * 4)
        x, y, uncovered = struct.unpack_from("<3f", result)
        self.assertAlmostEqual(x, 0.8)
        self.assertAlmostEqual(y, 0.9)
        self.assertEqual(uncovered, 1)

    def test_uncorrected_zero_retains_infinity_and_explicit_no_occlusion(self):
        data = make_masks()
        struct.pack_into("<f", data, 4, 0)
        layers = decode_masks(data, 0, scene_id=AK01, background_index=30)
        self.assertEqual(layers[0]["distance"], 0)
        self.assertIsNone(layers[0]["clip_depth"])
        self.assertEqual(layers[0]["native_depth"], "positive_infinity")
        self.assertEqual(layers[0]["native_depth_bits"], 0x7f800000)
        self.assertEqual(layers[0]["skip_reason"], "positive_infinity_has_no_finite_occlusion")
        self.assertGreater(sum(layers[0]["mask"].tobytes()), 0)  # Preserve original nonempty mask bits.
        result = depth_map(layers)
        self.assertEqual(struct.unpack_from("<f", result, (256 * 640) * 4)[0], 1)
        self.assertLess(struct.unpack_from("<f", result, 512 * 4)[0], 1)
        with self.assertRaises(ValueError):
            depth_map([{"mask": layers[0]["mask"], "clip_depth": math.inf}])
        layers[0]["distance"] = -1
        with self.assertRaises(ValueError):
            depth_map(layers)

    def test_rejects_truncation_invalid_offsets_and_distances(self):
        mutations = [lambda d: d[:3], lambda d: d[:100],
                     lambda d: struct.pack_into("<I", d, 24, 4) or d,
                     lambda d: struct.pack_into("<I", d, 24, len(d)) or d,
                     lambda d: struct.pack_into("<f", d, 4, math.nan) or d]
        for change in mutations:
            with self.subTest(change=change), self.assertRaises(ValueError):
                decode_masks(change(make_masks()), 0, scene_id=AK01, background_index=30)
        with self.assertRaises(ValueError):
            decode_masks(make_masks(), 0, scene_id=0, background_index=0)


if __name__ == "__main__":
    unittest.main()
