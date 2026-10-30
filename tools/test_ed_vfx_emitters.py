from pathlib import Path
import unittest

from ed_vfx import SourceRandom
from ed_vfx_hits import ReferenceHits
from ed_vfx_emitters import parse_emitter_effects,encode_emitter_effects,source_chainsaw_requests,source_birth_requests

GAME=Path(__file__).resolve().parents[1]/"game"


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class ActorEmittersTests(unittest.TestCase):
    def setUp(self):self.definitions=parse_emitter_effects((GAME/"EvilDead.exe").read_bytes())

    def test_original_chainsaw_and_birth_paths(self):
        self.assertEqual(source_chainsaw_requests(GAME),[(0,0,0,10,1,26,0)])
        self.assertEqual(source_birth_requests(GAME),[(25,4,0,0,0,0,3)])
        self.assertEqual(source_birth_requests(GAME,bonus=3),[])

    def test_source_constructor_curve_and_format(self):
        smoke,ring=self.definitions
        self.assertEqual((smoke["cap"],smoke["life"],smoke["size_min"],smoke["size_max"]),(100,32,70,100))
        self.assertEqual((smoke["frame_initial"],smoke["frame_delay"],smoke["columns"]*smoke["rows"]),(0,4,6))
        self.assertEqual((ring["life"],ring["ring_color"],ring["ring_segments"],ring["required_mode"]),(15,[128,0,0],16,3))
        self.assertEqual(len(encode_emitter_effects(self.definitions)),372)

    def test_smoke_phase_lifetime_and_ring_zero_size_rng(self):
        random=SourceRandom()
        fx=ReferenceHits(self.definitions,random)
        fx.spawn(0,[0,0,0]);initial=[p["tile"] for p in fx.active[0]]
        fx.spawn(25,[0,0,0]);self.assertEqual(random.calls[-1],("single",32767))
        fx.update()
        self.assertEqual([p["tile"] for p in fx.active[0]],[(tile+1)%6 for tile in initial])
        for _ in range(33):fx.update()
        self.assertFalse(fx.flat())


if __name__=="__main__":unittest.main()
