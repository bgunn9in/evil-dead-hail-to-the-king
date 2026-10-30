from pathlib import Path
from tempfile import TemporaryDirectory
import struct
import unittest

from ed_gui_assets import export_gui_assets, read_assets

ROOT = Path(__file__).resolve().parents[1]


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class GuiAssetsTests(unittest.TestCase):
    def test_original_scoped_text_and_maya_are_preserved(self):
        data = read_assets(ROOT / "game", "A")
        self.assertEqual(data["texts"][(0x61680200, 0xF1870800)], b"K0TEJ CTAH0BSTCR CYXSM.\0")
        self.assertNotEqual(data["texts"][(0x61630100, 0x91820800)], data["texts"][(0x616B0100, 0x91820800)])
        self.assertEqual(data["texture"].size, (256, 256))
        self.assertEqual(len(data["models"][0x0B142088]), 8)
        self.assertEqual(data["models"][0x0B142088][0]["triangles"], [[3, 0, 2], [0, 1, 2]])
        with TemporaryDirectory() as folder:
            output = Path(folder)
            result = export_gui_assets(ROOT / "game", output)
            self.assertEqual(result["text_count"], 443)
            self.assertEqual((output / "gui/text.bin").read_bytes()[:8], b"EGTEXT2\0")
            mesh = (output / "gui/models/game-over.mesh").read_bytes()
            self.assertEqual(struct.unpack_from("<8s3I", mesh), (b"EDOBJ1\0\0", 0x0B142088, 8, 48))
            self.assertEqual({p.name for p in output.iterdir()}, {"gui"})

    def test_original_b_has_separate_scope(self):
        data = read_assets(ROOT / "game", "B")
        self.assertEqual(len(data["texts"]), 392)
        self.assertIn((0x62670A00, 0x31A65000), data["texts"])
        self.assertNotIn((0x61680200, 0xF1870800), data["texts"])


if __name__ == "__main__":
    unittest.main()
