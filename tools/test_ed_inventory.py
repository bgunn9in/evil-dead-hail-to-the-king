import unittest
import struct
import tempfile
from pathlib import Path

from ed_inventory import export_runtime_inventory, read_inventory_assets
from export_scene import ROOT


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class InventoryAssetTests(unittest.TestCase):
    def test_original_vitamin_and_starter_cord_labels_and_icons(self):
        decoded = read_inventory_assets(ROOT / "game", [119, 29])
        vitamin, cord = decoded["items"]
        self.assertEqual((vitamin["flags"], vitamin["name_raw"], vitamin["shape_index"]), (60, "BSTAMSHZ", 12))
        self.assertEqual((cord["flags"], cord["name_raw"], cord["shape_index"]), (52, "TP0C", 3))
        self.assertEqual(vitamin["pixel_rect"], [0, 192, 128, 256])
        self.assertEqual(cord["pixel_rect"], [384, 0, 512, 64])
        self.assertEqual(decoded["textures"][0x0B942244].size, (512, 512))
        self.assertEqual(decoded["textures"][0x0B942244].mode, "RGBA")
        with self.assertRaises(ValueError):
            read_inventory_assets(ROOT / "game", [65535])

    def test_runtime_contract_keeps_source_alpha_and_icon_shape(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            result = export_runtime_inventory(ROOT / "game", folder, [119, 29, 119])
            self.assertEqual((result["items"], result["atlases"]), (2, 1))
            data = (folder / "inventory.bin").read_bytes()
            self.assertEqual(struct.unpack_from("<8s2I", data), (b"EDINV1\0\0", 1, 2))
            self.assertEqual(struct.unpack_from("<3I", data, 16), (0x0B942244, 512, 512))
            self.assertEqual(struct.unpack_from("<4I", data, 28), (119, 60, 0, 12))
            self.assertAlmostEqual(struct.unpack_from("<f", data, 60)[0], 1.28, places=5)
            pixels = (folder / "inventory_0b942244.rgba").read_bytes()
            self.assertEqual(len(pixels), 512 * 512 * 4)
            self.assertIn(0, pixels[3::4])
            self.assertIn(255, pixels[3::4])
        with self.assertRaises(ValueError):
            export_runtime_inventory(ROOT / "game", ROOT / "game/generated", [119])


if __name__ == "__main__":
    unittest.main()
