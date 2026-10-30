from pathlib import Path
import struct
import tempfile
import unittest
from ed_vfx import SourceRandom
from ed_vfx_ranged import parse_ranged_effects, encode_ranged_effects, ReferenceRanged, source_ranged_event, export_ranged_effects

GAME=Path(__file__).resolve().parents[1]/"game"


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class RangedTests(unittest.TestCase):
    def setUp(self):self.definitions=parse_ranged_effects((GAME/"EvilDead.exe").read_bytes())

    def test_source_definitions_and_event(self):
        shot,trail=self.definitions
        self.assertEqual((shot["kind"],shot["columns"],shot["rows"],shot["life"],shot["periodic_child"]),(7,4,8,30,22))
        self.assertEqual((trail["kind"],trail["life"],trail["color"],trail["random_frame"]),(22,4,[255,255,164,10],0))
        self.assertEqual(len(encode_ranged_effects(self.definitions)),332)
        self.assertEqual(struct.unpack_from("<HBBi",source_ranged_event(GAME)),(12,7,1,-1))

    def test_exact_rng_child_timer_life_and_export(self):
        random=SourceRandom();fx=ReferenceRanged(self.definitions,random)
        fx.spawn(7,[1,2,3],[1.25,2,3.5]);self.assertEqual(random.calls,[("cached",32)])
        fx.update();self.assertEqual((fx.child_timers[7],len(fx.active[22])),(2,1))
        self.assertEqual(fx.active[22][0]["position"],[1.25,2,3.5]);self.assertEqual(fx.active[22][0]["color"],[255,255,164,8])
        fx.update();fx.update();self.assertEqual(fx.child_timers[7],0)
        fx.update();self.assertEqual(len(fx.active[22]),2);self.assertEqual(random.calls,[("cached",32)])
        for _ in range(32):fx.update()
        self.assertFalse(fx.flat())
        with tempfile.TemporaryDirectory() as directory:
            export_ranged_effects(GAME,Path(directory));self.assertEqual((Path(directory)/"vfx/ranged-effects.bin").stat().st_size,332)
        with self.assertRaises(ValueError):export_ranged_effects(GAME,GAME)


if __name__=="__main__":unittest.main()
