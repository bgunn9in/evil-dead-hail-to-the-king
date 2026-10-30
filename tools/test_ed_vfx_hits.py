from pathlib import Path
import unittest

from ed_vfx import SourceRandom
from ed_vfx_hits import parse_hit_effects, encode_hit_effects, source_hit_requests, ReferenceHits

GAME = Path(__file__).resolve().parents[1] / "game"


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class HitEffectsTests(unittest.TestCase):
    def setUp(self):
        self.definitions = parse_hit_effects((GAME / "EvilDead.exe").read_bytes())

    def test_constructor_and_native_actor_event(self):
        blood, decal, shards = self.definitions
        self.assertEqual((blood["count_min"], blood["count_max"], blood["columns"]*blood["rows"]), (20,36,32))
        self.assertEqual(blood["uv"], [.5,0,.75,.25])
        self.assertEqual((decal["cap"],decal["life"],shards["rotation_step"]), (200,270,[2,8,5]))
        self.assertEqual(source_hit_requests(GAME), [(2,4,0,1,0,0,0)])
        self.assertEqual(len(encode_hit_effects(self.definitions)),444)

    def test_original_rng_order_recursive_death_and_lifetime(self):
        random = SourceRandom()
        fx = ReferenceHits(self.definitions,random)
        fx.spawn(2,[0,1,0])
        amount = len(fx.flat())
        self.assertTrue(20 <= amount < 36)
        self.assertEqual(random.calls[0],("single",16))
        self.assertEqual(random.calls[1:6],[("single",32767)]*4+[("cached",32)])
        for _ in range(50):
            fx.update()
        self.assertEqual(len(fx.active[2]),0)
        self.assertEqual(len(fx.active[18]),amount)
        self.assertTrue(all(p["position"][1]==0 for p in fx.active[18]))
        for _ in range(300):
            fx.update()
        self.assertEqual(fx.flat(),[])

    def test_coarse_gate_is_distinct_from_real_floor_and_bounce_truncates(self):
        fx = ReferenceHits(self.definitions,SourceRandom(),coarse=lambda x,z: 2,floor=lambda x,y,z: 1.9)
        fx.spawn(2,[0,1,0])
        fx.update()
        self.assertFalse(fx.active[2])
        self.assertTrue(all(abs(p["position"][1]-1.9)<1e-6 for p in fx.active[18]))
        fx.spawn(19,[0,.8,0])
        fx.update()
        self.assertTrue(all(p["position"][1] >= 1 for p in fx.active[19]))
        self.assertTrue(all(p["rotation"]==[2,8,5] for p in fx.active[19]))

    def test_rejected_group_cap_consumes_no_rng_and_clear_does_not_reset_sprite_phase(self):
        random=SourceRandom()
        fx=ReferenceHits(self.definitions,random)
        for _ in range(200):
            self.assertTrue(fx.spawn(18,[0,0,0]))
        before=list(random.calls)
        self.assertFalse(fx.spawn(18,[0,0,0]))
        self.assertEqual(random.calls,before)
        fx.update()
        self.assertEqual(fx.timers[2],1)  # Native node countdown advances even with an empty group.


if __name__ == "__main__":
    unittest.main()
