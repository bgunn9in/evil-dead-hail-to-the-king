from pathlib import Path
import struct
import unittest
from ed_vfx_variants import parse_variant_effects,encode_variant_effects,variant_events,ReferenceVariants,corona_secondary_geometry

GAME=Path(__file__).resolve().parents[1]/"game"

@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class VariantEffectsTests(unittest.TestCase):
    def test_original_mode_rows_and_all_real_child_modifiers(self):
        source=parse_variant_effects((GAME/"EvilDead.exe").read_bytes());fire,glow=source["definitions"]
        self.assertEqual((fire["kind"],fire["count_min"],fire["count_max"],fire["life"]),(3,4,10,11))
        self.assertEqual((glow["kind"],glow["count_min"],glow["count_max"],glow["life"]),(32,1,1,0))
        self.assertEqual(glow["children"],[{"flags":4,"kind":0,"chance":25},{"flags":4,"kind":22,"chance":15},{"flags":1,"kind":0,"chance":0}])
        self.assertEqual((source["ring"]["mode"],source["ring"]["segments"],source["ring"]["color"]),(1,16,[64,0,128]))
        self.assertEqual((source["corona"]["mode"],source["corona"]["segments"],source["corona"]["hemisphere"]),(1,8,0))
        self.assertEqual(struct.unpack_from("<8sI",encode_variant_effects(source)),(b"EDVFXV1\0",2))

    def test_exact_active_variant_records_have_original_archive_offsets(self):
        events=variant_events(GAME);self.assertEqual(len(events),4)
        self.assertEqual({event["actor"]for event in events},{"deadash","matriarchboss","anniezombie"})
        for event in events:
            blob=(GAME/"data"/event["archive"]).read_bytes();self.assertEqual(blob[event["archive_offset"]:event["archive_offset"]+36].hex(),event["raw"])

    def test_full_sphere_old_uv_age_and_render_mutation_order(self):
        from ed_vfx import SourceRandom,_f32 as f
        from ed_vfx_hits import parse_hit_effects
        from ed_vfx_emitters import parse_emitter_effects
        from ed_vfx_ranged import parse_ranged_effects
        from ed_vfx_tree import parse_tree_effects
        exe=(GAME/"EvilDead.exe").read_bytes();source=parse_variant_effects(exe)
        tint,geometry=corona_secondary_geometry(source["corona"],[0.,1.,0.],-1,3)
        self.assertEqual(len(geometry),432);self.assertEqual(geometry[0][:3],geometry[1][:3])
        self.assertEqual(tint,[204/255,102/255,0.,15/255]);self.assertEqual(432+12,444)
        _,wrong_uv=corona_secondary_geometry(source["corona"],[0.,1.,0.],-1,-1)
        self.assertNotEqual(geometry[0][3:],wrong_uv[0][3:])
        definitions=[parse_emitter_effects(exe)[0],parse_hit_effects(exe)[1],parse_ranged_effects(exe)[1],
            parse_tree_effects(exe)["definitions"][1],parse_emitter_effects(exe)[1],*source["definitions"]]
        random=SourceRandom();reference=ReferenceVariants(definitions,random)
        reference.spawn(24,[f(.2),1.,f(.2)],mode=1);parent=reference.active[24][0];serial=parent["serial"]
        reference.sample_render(0);self.assertEqual(parent["position"],[f(.2),1.,f(.2)]);self.assertEqual(parent["age"],0)
        self.assertEqual(reference.pending[serial],([f(.15),1.,f(.15)],-1))
        self.assertTrue(all(p["deferred"]for p in reference.active[0]+reference.active[3]));self.assertFalse(reference.active[32][0]["deferred"])
        seed,index=random.seed,random.index
        reference.sample_render(0);self.assertEqual((random.seed,random.index),(seed,index))
        reference.update();self.assertFalse(reference.active[24])

if __name__=="__main__":unittest.main()
