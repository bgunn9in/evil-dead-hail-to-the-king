import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from export_actor import ACTORS, LEVEL_B_ACTORS, B_STARTUP_ACTORS, export_actor, export_actors, levelb_actor_manifest, resource


GAME = Path(__file__).resolve().parents[1] / "game"


class LevelBProfileTests(unittest.TestCase):
    def test_levela_defaults_and_native31_resource_identity_stay_separate(self):
        self.assertEqual(len(ACTORS), 17)
        self.assertNotIn("badash", ACTORS)
        self.assertNotIn("guildmember", ACTORS)
        self.assertEqual(B_STARTUP_ACTORS, ("ash", "deadite", "skullbat"))
        variants = [definition for definition in LEVEL_B_ACTORS.values() if definition["native_type"] == 31]
        self.assertEqual(len(variants), 3)
        self.assertEqual({definition["capacity"] for definition in variants}, {4})
        self.assertEqual({definition["source_class"] for definition in variants}, {"NetherPet"})
        self.assertEqual({definition["mesh"] for definition in variants}, {0x0635E000, 0x06360000, 0x06362000})
        self.assertEqual({definition["packs"] for definition in variants}, {(0x0735E000, 0x07B5E0C4, 0x07B620C4)})


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class OriginalLevelBAssetsTests(unittest.TestCase):
    def test_b_startup_profile_uses_bz01_and_original_factories_only(self):
        with TemporaryDirectory() as temporary:
            output = Path(temporary)
            actors = export_actors(GAME, output, profile="B-startup")
            self.assertEqual(tuple(actors), B_STARTUP_ACTORS)
            manifest = json.loads((output / "levelb-actors.json").read_text())
            self.assertEqual(manifest["startup"]["method_index"], 1765)
            self.assertEqual(manifest["startup"]["native_pool_capacities"], {"0": 1, "4": 4, "2": 4})
            self.assertEqual((output / "scripts/levelB.jpc").read_bytes(), (GAME / "data/script/pc/levelB.jpc").read_bytes())
            az, bz = (GAME / "data/az01.pc").read_bytes(), (GAME / "data/bz01.pc").read_bytes()
            for name, metadata in actors.items():
                self.assertEqual(metadata["archive"], "bz01.pc")
                for key in ("mesh", "skeleton", "texture"):
                    identity = LEVEL_B_ACTORS[name][key]
                    self.assertEqual(resource(az, identity), resource(bz, identity))
            self.assertTrue(any(call.get("kind") == "direct LevelB.create factory"
                                for actor in manifest["actors"] for call in actor["source_factories"]))

    def test_three_netherpet_tuples_share_source_fsm_without_becoming_native36_or37(self):
        with TemporaryDirectory() as temporary:
            output = Path(temporary)
            names = ("netherpet", "netherpet_06360000", "netherpet_06362000")
            metadata = [export_actor(GAME, output, name) for name in names]
            self.assertEqual({record["native_type"] for record in metadata}, {31})
            self.assertEqual([record["source_prototypes"] for record in metadata], [[31], [36], [37]])
            self.assertEqual({record["clip_count"] for record in metadata}, {29})
            self.assertEqual({record["bone_count"] for record in metadata}, {17})
            self.assertEqual(len({record["actor_sha256"] for record in metadata}), 3)
            self.assertEqual(len({(output / "actors" / name / "actions.bin").read_bytes() for name in names}), 1)
            manifest = levelb_actor_manifest(GAME, output, names)
            self.assertEqual({actor["native"]["fsm_table"] for actor in manifest["actors"]}, {"0x467bd8"})
            self.assertEqual({actor["script_pool"]["base"] for actor in manifest["actors"]}, {60})


if __name__ == "__main__":
    unittest.main()
