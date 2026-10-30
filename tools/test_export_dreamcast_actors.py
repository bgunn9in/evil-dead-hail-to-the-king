import hashlib
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from ed_actor_textures import read_actor_textures
from ed_movies import ROOT
from export_actor import ACTORS, export_actor
from export_dreamcast_actors import export_profiles

SOURCE = ROOT / "build/dreamcast-original-20261027/files"


class DreamcastActorExportTests(unittest.TestCase):
    def test_original_and_existing_outputs_rejected(self):
        with TemporaryDirectory() as directory:
            source = Path(directory) / "source"
            source.mkdir()
            for output in (source, source / "derived", ROOT / "game/derived"):
                with self.assertRaises(ValueError):
                    export_profiles(source, output)
            with self.assertRaises(FileExistsError):
                export_profiles(source, Path(directory))

    @unittest.skipUnless((SOURCE / "AZ01.DC").is_file(), "Requires original Dreamcast files")
    def test_real_ash_and_exact_jdc_texture_variants(self):
        original = (SOURCE / "AZ01.DC").read_bytes()
        with TemporaryDirectory() as directory:
            output = Path(directory)
            metadata = export_actor(SOURCE, output, "ash", dreamcast=True)
            self.assertEqual((metadata["platform"], metadata["archive"], metadata["vertex_count"]), ("Dreamcast", "AZ01.DC", 1590))
            self.assertEqual(metadata["texture_name"]["tail_bytes"], 14304)
            self.assertEqual(metadata["executable_sha256"], "de26cba0d4e6035d1128daf504a2257a695050bac11ecdf6e9e2e0baf0f26841")
            self.assertEqual((output / "actors/ash/actor-parts.bin").read_bytes()[:8], b"EDACTOR2")
            self.assertEqual(hashlib.sha256((output / "actors/ash/actor.bin").read_bytes()).hexdigest(), metadata["actor_sha256"])
            self.assertEqual((output / "actions.bin").read_bytes(), (output / "actors/ash/actions.bin").read_bytes())
        textures = read_actor_textures(SOURCE, {name: d["texture"] for name, d in ACTORS.items()}, dreamcast=True)
        self.assertEqual((textures["archive_count"], len(textures["textures"])), (140, 24))
        self.assertEqual({p["path"] for p in textures["packages"]}, {"LEVELA.JDC", "LEVELB.JDC"})
        self.assertEqual(sum(t["variant"] for t in textures["textures"]), 7)
        self.assertEqual((SOURCE / "AZ01.DC").read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
