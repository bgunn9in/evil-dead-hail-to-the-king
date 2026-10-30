from pathlib import Path
import struct
import unittest

from ed_animation_sounds import audit_animation_sounds,verify_audio_manifest,TYPES
from export_actor import ACTORS,LEVEL_B_ACTORS

GAME=Path(__file__).resolve().parents[1]/"game"

@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class AnimationSoundTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.audit=audit_animation_sounds(GAME)

    def test_all_seventeen_supported_source_actor_records_resolve_without_aliases(self):
        self.assertEqual(self.audit["level"],"A");self.assertEqual(set(TYPES),set(ACTORS));self.assertEqual(len(self.audit["actors"]),17)
        for actor in self.audit["actors"]:
            blob=(GAME/"data"/actor["archive"]).read_bytes()
            for record in actor["records"]:
                raw=bytes.fromhex(record["raw"]);self.assertEqual(blob[record["archive_offset"]:record["archive_offset"]+20],raw)
                self.assertEqual(struct.unpack_from("<I",raw)[0],record["resource_id"])
                if record["active"]:self.assertTrue(record["wave_sha256"])
        ash=next(a for a in self.audit["actors"]if a["name"]=="ash")
        self.assertEqual((ash["reachable_clips"],len(ash["records"])),(56,58))
        self.assertTrue({0x10000,0x10002,0x10004,0x10005,0x10007}<={r["resource_id"]for r in ash["records"]})

    def test_level_b_exact_records_and_three_pet_models(self):
        b=audit_animation_sounds(GAME,"B");self.assertEqual(b["level"],"B");self.assertEqual(b["global_bucket"],"global_b")
        self.assertEqual({a["name"]for a in b["actors"]},set(LEVEL_B_ACTORS))
        self.assertEqual({a["native_type"]for a in b["actors"]},{0,2,4,19,23,24,26,27,28,29,31,33,38,39})
        for actor in b["actors"]:
            blob=(GAME/"data"/actor["archive"]).read_bytes()
            for record in actor["records"]:
                raw=bytes.fromhex(record["raw"]);self.assertEqual(blob[record["archive_offset"]:record["archive_offset"]+20],raw)
                if record["active"]:self.assertTrue(record["wave_sha256"])
        pets=[a for a in b["actors"]if a["native_type"]==31]
        self.assertEqual(len(pets),3);self.assertEqual([sum(r["active"]for r in a["records"])for a in pets],[21]*3)
        self.assertEqual(pets[0]["records"],pets[1]["records"]);self.assertEqual(pets[1]["records"],pets[2]["records"])
        output=GAME.parent/"build/levelb/assets"
        if(output/"audio/manifest.txt").exists():self.assertEqual(verify_audio_manifest(GAME,output,b)["missing"],[])

    def test_level_a_and_b_have_the_same_verified_callback_and_footstep_logic(self):
        a,b=self.audit["scripts"]
        self.assertEqual([(m["index"],m["code_sha256"])for m in a["methods"]],[(m["index"],m["code_sha256"])for m in b["methods"]])
        ash=next(m for m in a["methods"]if m["index"]==658)
        self.assertEqual(ash["resource_cases"],[[0x10000,52],[0x10001,416],[0x10002,159],[0x10003,416],
            [0x10004,235],[0x10005,340],[0x10006,416],[0x10007,311]])

if __name__=="__main__":unittest.main()
