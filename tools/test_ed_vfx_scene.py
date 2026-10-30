from pathlib import Path
import struct
import unittest
from ed_vfx_scene import script_requests,scene_geometry_goldens
from ed_vfx_variants import parse_variant_effects

GAME=Path(__file__).resolve().parents[1]/"game"

@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class SceneVfxTests(unittest.TestCase):
    def test_entire_original_jpc_rock_and_fireball_methods(self):
        cases=script_requests(GAME)
        self.assertEqual(cases["dropRockOnAsh"]["requests"],[("vfxSpawnActorOffset",[15,0,0,0,700,0,1]),
            ("vfxSpawnActorOffset",[15,0,0,-20,500,-20,1]),("vfxSpawnActorOffset",[15,0,0,20,600,20,1])])
        self.assertEqual(cases["dropRockOnAsh"]["messages"],[[0,0,40,0,0,3,0,0]])
        self.assertEqual(cases["doAttackFireBall"]["requests"],[("vfxSpawnActorJointRnd",[24,33,0,0,1,1,1])])

    def test_source_bg12_table_rows_geometry_is_bounded(self):
        source=parse_variant_effects((GAME/"EvilDead.exe").read_bytes())
        self.assertEqual([(r["mode"],r["segments"])for r in source["script_rings"]],[(11,16),(12,8)])
        self.assertEqual((source["script_corona"]["mode"],source["script_corona"]["hemisphere"]),(6,1))
        data=scene_geometry_goldens(GAME,source);self.assertEqual(struct.unpack_from("<8sI",data),(b"EDVFXSG2",12));self.assertLess(len(data),64*1024)

if __name__=="__main__":unittest.main()
