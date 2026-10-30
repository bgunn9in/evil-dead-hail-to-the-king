import math
from pathlib import Path
import struct
import tempfile
import unittest

from ed_objects import encode_mesh, encode_texture, export_runtime_objects, object_rotation


GAME = Path(__file__).resolve().parents[1] / "game"


def part():
    return {"flags_raw": 0, "positions": [[0, 0, 0], [1, 0, 0], [0, 1, 0]],
            "normals": [[0, 0, 1]] * 3, "uv": [[0, 0], [1, 0], [0, 1]],
            "colors": [0xFFFFFF01] * 3, "bone_indices": [65535] * 3, "triangles": [[0, 1, 2]]}


class FormatTests(unittest.TestCase):
    def test_preserves_original_static_colors_parts_and_triangle_order(self):
        data, vertices = encode_mesh(0x0B018328, [part()])
        self.assertEqual(struct.unpack_from("<8s3I", data), (b"EDOBJ1\0\0", 0x0B018328, 1, 3))
        self.assertEqual(len(data), 24 + 3 * 40)
        self.assertEqual(struct.unpack_from("<IHH", data, 24 + 32), (0xFFFFFF01, 0, 0))
        self.assertEqual(vertices[1][:3], (1, 0, 0))

    def test_rejects_deformation_and_nonfinite_geometry(self):
        broken = part()
        broken["bone_indices"][0] = 0
        with self.assertRaises(ValueError):
            encode_mesh(1, [broken])
        broken = part()
        broken["positions"][0][0] = math.nan
        with self.assertRaises(ValueError):
            encode_mesh(1, [broken])

    def test_raw_texture_preserves_alpha_and_dimensions(self):
        from PIL import Image
        image = Image.new("RGBA", (2, 1))
        image.putdata([(1, 2, 3, 0), (4, 5, 6, 127)])
        data = encode_texture(7, image)
        self.assertEqual(struct.unpack_from("<8s3I", data), (b"EDTEX1\0\0", 7, 2, 1))
        self.assertEqual(data[20:], bytes((1, 2, 3, 0, 4, 5, 6, 127)))

    def test_source_euler_matrix_order(self):
        rotation = object_rotation((0, 90, 0))
        self.assertAlmostEqual(rotation[0][2], -1, places=6)
        self.assertAlmostEqual(rotation[2][0], 1, places=6)
        # Rz90 thenRy90 thenRx90 maps local+X to local+Z, not+Y.
        rotation = object_rotation((90, 90, 90))
        self.assertAlmostEqual(rotation[0][2], 1, places=6)
        self.assertAlmostEqual(rotation[0][1], 0, places=6)


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class OriginalObjectsTests(unittest.TestCase):
    def test_actual_scene_and_loot_resources_without_aliasing(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest = export_runtime_objects(GAME, Path(directory))
            self.assertEqual(len(manifest["models"]), 19)
            self.assertEqual(len(manifest["textures"]), 3)
            self.assertEqual({entry["resource"] for entry in manifest["textures"]}, {0x0BCC0004, 0x0BCC2004, 0x0BCD6004})
            exact = {entry["resource"]: entry for entry in manifest["models"]}
            self.assertEqual(exact[0x0B01832C]["archive"], "az00.pc")
            self.assertEqual(exact[0x0B018354]["archive"], "az00.pc")
            self.assertTrue(all(entry["part_count"] == 1 for entry in exact.values()))
            axe_ammo = [entry for entry in manifest["objects"] if entry["item"] == 4][0]
            self.assertEqual(axe_ammo["mesh"], 0x0B01832C)
            self.assertEqual(axe_ammo["loot_producer"][0]["method"], 897)
            self.assertTrue((Path(directory) / "objects/pose-goldens.bin").is_file())


if __name__ == "__main__":
    unittest.main()
