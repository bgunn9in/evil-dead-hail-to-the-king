from pathlib import Path
import struct
import unittest
from ed_vfx_npc import parse_npc_effects,npc_events,source_callback,encode_npc_effects,npc_goldens,combat_callback,parse_blood_effect,blood_events,blood_goldens,parse_sixteen_effect,sixteen_events,sixteen_goldens,combat_events,combat_dispatch_audit

GAME=Path(__file__).resolve().parents[1]/"game"

@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class NpcParticleTests(unittest.TestCase):
    def test_original_seventeen_constructor_and_modifier_parameters(self):
        d=parse_npc_effects((GAME/"EvilDead.exe").read_bytes())[0]
        self.assertEqual((d["kind"],d["life"],d["color"],d["delta"]),(17,15,[255,255,255,96],[-4,0,0,-3]))
        self.assertEqual((d["rotation_step"],d["random_child"],d["random_child_chance"]),([0,16,0],32,75))
        self.assertEqual((d["count_min"],d["count_max"],d["collision"],d["direction_scale"]),(1,1,0,0))
        self.assertEqual(struct.unpack_from("<8sI",encode_npc_effects([d])),(b"EDVFXN1\0",1))

    def test_all_ten_original_reachable_records_have_exact_offsets(self):
        events=npc_events(GAME);self.assertEqual(len(events),10)
        self.assertEqual(sum(e["actor"]=="deadite"for e in events),9)
        for event in events:
            blob=(GAME/"data"/event["archive"]).read_bytes();self.assertEqual(blob[event["archive_offset"]:event["archive_offset"]+36].hex(),event["raw"])

    def test_actual_jpc_consumes_only_pet_types_in_both_packages(self):
        a,b=source_callback(GAME)
        self.assertEqual(a["cases"],b["cases"])
        self.assertEqual([c["script_field6"]for c in a["cases"]if c["consumed"]],[31,36,37])
        self.assertEqual([m["sha256"]for m in a["methods"]],[m["sha256"]for m in b["methods"]])
        self.assertEqual(struct.unpack_from("<8s2I",npc_goldens(GAME)),(b"EDVFXNG1",140,2))

    def test_original_combat_twenty_consumption_and_shared_rng(self):
        a,b=combat_callback(GAME);self.assertEqual(a["cases"],b["cases"])
        self.assertEqual([c["consumed"]for c in a["cases"]],[True,True,True,False,True,False,True,False,False])
        self.assertEqual(a["cases"][0]["random_bounds"],[])
        self.assertTrue(all(c["random_bounds"]==[100]for c in a["cases"][1:]))

    def test_native_twenty_one_is_separate_directional_blood(self):
        d=parse_blood_effect((GAME/"EvilDead.exe").read_bytes())
        self.assertEqual((d["kind"],d["count_min"],d["count_max"],d["direction_scale"]),(21,30,50,.5))
        self.assertEqual((d["life"],d["collision"],d["child_death"],d["random_child_chance"]),(0,1,18,0))
        self.assertEqual((d["size_min"],d["size_max"],d["growth"],d["color"],d["delta"]),(40.,60.,1.,[255,255,255,128],[0,0,0,-6]))
        self.assertEqual((d["uv"],d["columns"],d["rows"]),([.5,0,.75,.25],4,8))

    def test_twenty_one_original_records_and_goldens(self):
        events=blood_events(GAME);self.assertEqual(len(events),45)
        self.assertEqual(sum(e["level"]=="A"for e in events),27)
        for event in events:
            blob=(GAME/"data"/event["archive"]).read_bytes();self.assertEqual(blob[event["archive_offset"]:event["archive_offset"]+36].hex(),event["raw"])
        self.assertEqual(struct.unpack_from("<8s2I",blood_goldens(GAME)),(b"EDVFXBG1",320,2))

    def test_native_sixteen_jitter_and_ordered_children(self):
        d=parse_sixteen_effect((GAME/"EvilDead.exe").read_bytes())
        self.assertEqual((d["kind"],d["life"],d["size"],d["growth"],d["count_min"],d["count_max"]),(16,60,60.,-1.,8,16))
        self.assertEqual((d["color"],d["delta"],d["jitter_min_age"],d["bounce_percent"]),([32,128,32,224],[4,0,0,-2],16,[10.]*3))
        self.assertEqual(d["children"],[{"flags":4,"kind":9,"chance":15},{"flags":4,"kind":22,"chance":2}])
        self.assertEqual(len(sixteen_events(GAME)),8)
        self.assertEqual(struct.unpack_from("<8s2I",sixteen_goldens(GAME)),(b"EDVFX16G",220,2))

    def test_all_original_npc_twenty_caller_records(self):
        rows=combat_events(GAME);self.assertEqual(len(rows),303)
        self.assertEqual(sum(e["level"]=="A"for e in rows),203)
        self.assertEqual({e["native_type"]for e in rows},{2,4,7,8,9,10,11,12,23})
        for event in rows:
            raw=bytes.fromhex(event["raw"]);self.assertEqual(struct.unpack_from("<i",raw,4)[0],-1)
            blob=(GAME/"data"/event["archive"]).read_bytes();self.assertEqual(blob[event["archive_offset"]:event["archive_offset"]+36],raw)

    def test_actual_derived_classes_share_the_original_twenty_predicate(self):
        a,b=combat_dispatch_audit(GAME);self.assertEqual(a["method1525_sha256"],b["method1525_sha256"])
        self.assertEqual((len(a["rows"]),len(b["rows"])),(8,3))
        for row in a["rows"]+b["rows"]:
            self.assertEqual(row["resolved_method"],1525);self.assertEqual(row["class_chain"][-1],"GenericNpc")
            self.assertEqual([c["consumed"]for c in row["cases"]],[True,True,False,False])

if __name__=="__main__":unittest.main()
