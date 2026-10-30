import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from ed_movies import ROOT
from export_dreamcast_objects import export_pools

SOURCE = ROOT / "build/dreamcast-original-20261027/files"


class DreamcastObjectExportTests(unittest.TestCase):
    def test_output_guards_and_empty_source(self):
        with TemporaryDirectory() as directory:
            source = Path(directory) / "original"
            source.mkdir()
            for output in (source, source / "derived", ROOT / "game/derived"):
                with self.assertRaises(ValueError):
                    export_pools(source, output)
            with self.assertRaises(FileExistsError):
                export_pools(source, Path(directory))
            with self.assertRaises(ValueError):
                export_pools(source, Path(directory) / "empty")
            self.assertFalse((Path(directory) / "empty").exists())

    @unittest.skipUnless((SOURCE / "AZ00.DC").is_file(), "Requires original Dreamcast archives")
    def test_original_complete_scoped_pools_keep_exact_ids_and_sources(self):
        with TemporaryDirectory() as directory:
            output = Path(directory) / "generated"
            result = export_pools(SOURCE, output)
            self.assertEqual(len(result["archives"]), 140)
            self.assertEqual(result["profiles"], {"A": {"models": 134, "textures": 54},
                                                  "B": {"models": 139, "textures": 49}})
            self.assertFalse((output / "INCOMPLETE").exists())
            for level in ("A", "B"):
                root = output / level / "objects"
                manifest = json.loads((root / "source.json").read_text("utf-8"))
                self.assertEqual(len(list((root / "models").glob("*.bin"))), result["profiles"][level]["models"])
                self.assertEqual(len(list((root / "textures").glob("*.rgba"))), result["profiles"][level]["textures"])
                for model in manifest["models"]:
                    self.assertTrue(all(s["id"] == model["id"] for s in model["sources"]))
                    self.assertTrue((root / "models" / f"{model['id']:08x}.bin").read_bytes().startswith(b"EDOBJ1\0\0"))
                self.assertTrue(any(t["id"] == 0x0816E044 for t in manifest["textures"]))
            with self.assertRaises(FileExistsError):
                export_pools(SOURCE, output)


if __name__ == "__main__":
    unittest.main()
