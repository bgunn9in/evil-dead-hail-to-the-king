import copy
import hashlib
import json
from pathlib import Path
import struct
from tempfile import TemporaryDirectory
import unittest

from export_menu import (ROOT, MAIN_GEOMETRY, MAIN_TEXTURE, export_menu, menu_context,
                         menu_script_audit, pack_resources, read_resources, resource_pool, media_scope, seed_movie_cache)


class MenuResourceTests(unittest.TestCase):
    def test_media_union_keeps_episode_and_prior_scopes(self):
        with TemporaryDirectory() as scratch:
            game, output = Path(scratch) / "game", Path(scratch) / "generated"
            (game / "data/script/pc").mkdir(parents=True)
            (game / "data/script/pc/levelA.jpc").write_bytes(b"sourceJPC")
            (game / "fmv").mkdir()
            for name in ("ax00", "ax01a", "ax02", "ax03a"):
                (game / f"fmv/{name}.bik").write_bytes(name.encode())
            hashes = {}
            for name in ("az01", "bz01", "ak01", "ak09", "al01"):
                raw = name.encode();(game / f"data/{name}.pc").write_bytes(raw)
                hashes[name] = hashlib.sha256(raw).hexdigest()
            (output / "audio").mkdir(parents=True)
            (output / "movies").mkdir()
            (output / "episode.json").write_text(json.dumps({"scenes": ["ak01", "ak09"], "movie_ids": ["0x61303200"],
                "source_script_sha256": hashlib.sha256(b"sourceJPC").hexdigest()}))
            (output / "audio/source.json").write_text(json.dumps({"format": "EDAUDIO1", "source_hashes": hashes}))
            (output / "movies/manifest.json").write_text(json.dumps({"format": "EDMOVIE1", "movies": [{"id": 0x61303361}]}))
            scope = media_scope(game, output, "A", [{"movie": "ax00"}, {"movie": "ax01a"}])
            self.assertEqual(scope["audio_scenes"], ["ak01", "ak09", "al01", "aa01"])
            self.assertEqual(set(scope["movie_ids"]), {0x61303200, 0x61303361, 0x61303000, 0x61303161})
            self.assertEqual(json.loads((output / "episode.json").read_text())["scenes"], ["ak01", "ak09"])
            with self.assertRaises(ValueError):
                media_scope(game, output, "B", [])
            (game / "data/al01.pc").write_bytes(b"changed")
            with self.assertRaises(ValueError):
                media_scope(game, output, "A", [])

    def test_verified_generated_movie_cache_avoids_source_redecode(self):
        with TemporaryDirectory() as scratch:
            game, output, cache = (Path(scratch) / name for name in ("game", "output", "cache"))
            (game / "fmv").mkdir(parents=True);output.mkdir();(cache / "movies").mkdir(parents=True)
            source, converted = b"originalBink", b"decodedEDMOVIE1"
            (game / "fmv/ax00.bik").write_bytes(source)
            (cache / "movies/61303000.edm").write_bytes(converted)
            metadata = {"format": "EDMOVIE1", "id": 0x61303000, "source_sha256": hashlib.sha256(source).hexdigest(),
                        "decoded_bytes": len(converted), "decoded_sha256": hashlib.sha256(converted).hexdigest()}
            (cache / "movies/61303000.json").write_text(json.dumps(metadata))
            self.assertEqual(seed_movie_cache(game, output, cache, [0x61303000]), [0x61303000])
            self.assertEqual((output / "movies/61303000.edm").read_bytes(), converted)
            stamp = (output / "movies/61303000.edm").stat().st_mtime_ns
            self.assertEqual(seed_movie_cache(game, output, cache, [0x61303000]), [])
            self.assertEqual((output / "movies/61303000.edm").stat().st_mtime_ns, stamp)
            fresh = Path(scratch) / "fresh";fresh.mkdir();(cache / "movies/61303000.edm").write_bytes(b"bad")
            with self.assertRaises(ValueError):
                seed_movie_cache(game, fresh, cache, [0x61303000])

    def test_exact_flags_identity_and_byte_ranges(self):
        records = [{"id": 7, "flags": 0, "payload": b"mesh"}, {"id": 7, "flags": 1, "payload": b"DDS"}]
        encoded = pack_resources(records)
        self.assertEqual(read_resources(encoded), {(7, 0): b"mesh", (7, 1): b"DDS"})
        duplicate = records + [copy.deepcopy(records[0])]
        with self.assertRaises(ValueError):
            pack_resources(duplicate)
        for damaged in (encoded[:-1], encoded + b"0", b"EDMRES1\0" + struct.pack("<I", 2001)):
            with self.assertRaises(ValueError):
                read_resources(damaged)
        bad = bytearray(encoded)
        struct.pack_into("<I", bad, 20, 0xFFFFFFFF)
        with self.assertRaises(ValueError):
            read_resources(bad)

    @unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
    def test_actual_menu_floor_and_absent_spawn_are_not_fabricated(self):
        for level, count in (("A", 4), ("B", 3)):
            context = menu_context(ROOT / "game", level)
            self.assertEqual(len(context["scene"]["tracks"]), count)
            self.assertEqual(context["scene"]["markers"], [])
            self.assertEqual(context["scene"]["navpoints"], [])
            self.assertEqual(len(context["collision"]["floor"]["triangles"]), 2)
            audit = menu_script_audit(ROOT / "game", level)
            self.assertTrue(audit["load_initially_available"])
            self.assertEqual(audit["pc_input"]["confirm_item_event"], 111)
            self.assertEqual({row["movie"] for row in audit["movies"]}, {"ax00", "ax01a"})
            pool = read_resources(pack_resources(resource_pool(ROOT / "game", level)))
            self.assertIn((MAIN_GEOMETRY, 0), pool)
            self.assertIn((MAIN_TEXTURE, 1), pool)

    @unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
    def test_actual_export_has_menu_contract_and_original_native_bytes(self):
        with TemporaryDirectory() as scratch:
            result = export_menu(ROOT / "game", Path(scratch), "A")
            directory = Path(scratch) / "aa01"
            self.assertEqual((directory / "menu.txt").read_text(), "EDMENU1\naa01\n4\n")
            self.assertFalse((directory / "scene.txt").exists())
            self.assertEqual((directory / "scene.native").read_bytes(), menu_context(ROOT / "game", "A")["payload"])
            self.assertIsNone(result["spawn"])
            self.assertFalse(result["walking"])
            self.assertEqual(len((directory / "camera-frames.bin").read_bytes()), 12 + 4 * (4 + 29 * 4))
            self.assertEqual(len(list(directory.glob("background_*.bmp"))), 4)
            self.assertEqual(len(list(directory.glob("background_*.depth"))), 4)
        with self.assertRaises(ValueError):
            export_menu(ROOT / "game", ROOT / "game/generated-menu", "A")


if __name__ == "__main__":
    unittest.main()
