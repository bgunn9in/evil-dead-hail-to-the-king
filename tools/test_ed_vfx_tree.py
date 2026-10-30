from collections import Counter
from pathlib import Path
import math
import struct
import unittest

from ed_vfx_tree import parse_tree_effects,tree_animation_events,tree_requests,tree_goldens,export_tree_effects,corona_geometry
from ed_audio import read_audio_assets

GAME=Path(__file__).resolve().parents[1]/"game"

@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class TreeVfxTests(unittest.TestCase):
    def test_reachable_original_fsm_records_and_sfx_offsets(self):
        source=tree_animation_events(GAME);blob=(GAME/"data/at02.pc").read_bytes();counts=Counter();sounds=[]
        self.assertEqual(len(source["clips"]),9)
        for clip in source["clips"]:
            for n,raw in enumerate(clip["events36"]):
                self.assertEqual(blob[clip["archive_offset36"]+n*36:clip["archive_offset36"]+(n+1)*36].hex(),raw)
                counts[bytes.fromhex(raw)[2]]+=1
            for n,raw in enumerate(clip["events20"]):
                self.assertEqual(blob[clip["archive_offset20"]+n*20:clip["archive_offset20"]+(n+1)*20].hex(),raw);sounds.append(bytes.fromhex(raw))
        self.assertEqual(counts,{0:3,9:59,15:9,24:3,25:12,34:2,35:4});self.assertEqual(len(sounds),15)
        clips={c["id"]for c in read_audio_assets(GAME,["at02"])["clips"]if c["bucket"]in("at02","global")}
        self.assertTrue(all(struct.unpack_from("<I",raw)[0]in clips for raw in sounds))

    def test_original_debris_corona_ring_parameters(self):
        source=parse_tree_effects((GAME/"EvilDead.exe").read_bytes());debris,corona=source["definitions"]
        self.assertEqual((debris["count_min"],debris["count_max"],debris["life"],debris["size"],debris["random_child_chance"]),(16,24,60,50,25))
        self.assertEqual((debris["collision"],debris["bounce_percent"],debris["random_child"]),(2,[10]*3,0))
        self.assertEqual((corona["kind"],corona["life"],corona["columns"],corona["rows"]),(24,15,4,2))
        self.assertEqual([r["mode"]for r in source["rings"]],[0,2]);self.assertEqual(source["corona_mode"],4)
        self.assertEqual(len(tree_requests(GAME)),7)

    def test_independent_particle_rng_and_render_goldens_bounded(self):
        source=parse_tree_effects((GAME/"EvilDead.exe").read_bytes());data=tree_goldens(GAME,source)
        self.assertEqual(struct.unpack_from("<8s2I",data),(b"EDVFXTG1",104,7));self.assertLess(len(data),2*1024*1024)

    def test_export_rejects_original_paths_before_writing(self):
        from ed_vfx import export_runtime_vfx
        with self.assertRaises(ValueError):export_tree_effects(GAME,GAME)
        with self.assertRaises(ValueError):export_runtime_vfx(GAME,GAME/"generated")

    def test_corona_original_geometry_uv_alpha_and_explicit_safe_rgb(self):
        source=parse_tree_effects((GAME/"EvilDead.exe").read_bytes())
        for age in (0,1,8,16):
            tint,vertices=corona_geometry(source,[0.,0.,0.],age)
            self.assertEqual(len(vertices),216);self.assertEqual(vertices[0][:3],vertices[1][:3]) # Original j0 degenerate geometry, distinct UVs.
            self.assertEqual(tint[:3],[0.,(96-6*age)/255,(192-12*age)/255])
            self.assertEqual(tint[3],(((0xff000000-(age<<28))&0xffffffff)>>24)/255)
            self.assertTrue(all(math.isfinite(x)for vertex in vertices for x in vertex))

    def test_temporary_mode_one_debris_parameters_are_restored(self):
        from ed_vfx import SourceRandom
        from ed_vfx_tree import ReferenceTree
        d=parse_tree_effects((GAME/"EvilDead.exe").read_bytes())["definitions"][0]
        reference=ReferenceTree([d],SourceRandom());reference.spawn(15,[0.,0.,0.],mode=1)
        self.assertEqual(reference.active[15][0]["color"][:3],[255]*3)
        reference.spawn(15,[0.,0.,0.],mode=-1)
        self.assertEqual(reference.active[15][0]["mode"],1)
        self.assertEqual(reference.active[15][0]["color"][:3],[128]*3)

if __name__=="__main__":unittest.main()
