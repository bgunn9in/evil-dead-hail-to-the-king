from pathlib import Path
import struct
import unittest
from ed_vfx_ash import parse_ash_effects,ash_particle_events,source_combat_counter,ash_goldens,export_ash_effects,parse_shell_effects,shell_repulsion,shell_event,shell_goldens,source_misc_callbacks,parse_flash_effects,flash_goldens,ash_animation_events

GAME=Path(__file__).resolve().parents[1]/"game"

@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class AshParticleTests(unittest.TestCase):
    def test_original_constructor_and_combat_callback(self):
        d=parse_ash_effects((GAME/"EvilDead.exe").read_bytes())[0]
        self.assertEqual((d["kind"],d["count_min"],d["count_max"],d["life"],d["columns"],d["rows"]),(20,3,6,0,1,4))
        self.assertEqual((d["child_death"],d["random_child"],d["random_child_chance"]),(18,18,5))
        self.assertEqual(source_combat_counter(GAME)["accepted_after_combo"],100)

    def test_actual_records_and_independent_goldens(self):
        blob=(GAME/"data/az01.pc").read_bytes();events=ash_particle_events(GAME);self.assertGreater(len(events),100)
        for event in events:self.assertEqual(blob[event["archive_offset"]:event["archive_offset"]+36].hex(),event["raw"])
        data=ash_goldens(GAME,parse_ash_effects((GAME/"EvilDead.exe").read_bytes()))
        self.assertEqual(struct.unpack_from("<8sI",data),(b"EDVFXAG1",140));self.assertLess(len(data),1024*1024)
        with self.assertRaises(ValueError):export_ash_effects(GAME,GAME)

    def test_shell_source_constructor_and_physical_actor_force(self):
        d=parse_shell_effects((GAME/"EvilDead.exe").read_bytes())
        self.assertEqual((d["kind"],d["size"],d["collision"],d["bounce_percent"]),(8,25.,2,[25.]*3))
        actors=[None]*13;actors[0]={"flags":1,"local":[0.,0.,0.],"floor":0.}
        center=shell_repulsion([0.,0.,0.],[0.,0.,0.],actors)
        self.assertEqual(center,[-4.7434163093566895,.9486833810806274,-4.7434163093566895])
        actors[0]["flags"]=9;self.assertEqual(shell_repulsion([0.]*3,[0.]*3,actors),[0.]*3)
        event=shell_event(GAME);blob=(GAME/"data/az01.pc").read_bytes();self.assertEqual(blob[event["archive_offset"]:event["archive_offset"]+36].hex(),event["raw"])
        self.assertEqual(struct.unpack_from("<8sI",shell_goldens(GAME)),(b"EDVFXSG1",240))

    def test_misc_source_records_and_whole_original_callback(self):
        from collections import Counter
        result=source_misc_callbacks(GAME);blob=(GAME/"data/az01.pc").read_bytes();counts=Counter()
        for event in result["events"]:
            raw=bytes.fromhex(event["raw"]);self.assertEqual(blob[event["archive_offset"]:event["archive_offset"]+36],raw);counts[raw[2]]+=1
        self.assertEqual(counts,{0:15,9:52,35:12});a,b=result["packages"]
        self.assertEqual([m["code_sha256"]for m in a["methods"]],[m["code_sha256"]for m in b["methods"]])

    def test_original_flash_six_is_fixed_and_consumes_no_rng(self):
        d=parse_flash_effects((GAME/"EvilDead.exe").read_bytes())
        self.assertEqual((d["kind"],d["size"],d["growth"],d["life"],d["random_frame"]),(6,50.,10.,4,0))
        self.assertEqual((d["color"],d["delta"],d["uv"]),([255,255,255,128],[0,0,0,-32],[.25,.25,.375,.375]))
        events=ash_animation_events(GAME,{6});self.assertEqual(len(events),9);blob=(GAME/"data/az01.pc").read_bytes()
        for event in events:self.assertEqual(blob[event["archive_offset"]:event["archive_offset"]+36].hex(),event["raw"])
        self.assertEqual(struct.unpack_from("<8s2I",flash_goldens(GAME)),(b"EDVFXFG1",12,2))

if __name__=="__main__":unittest.main()
