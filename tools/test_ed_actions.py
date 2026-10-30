from pathlib import Path
import struct
import unittest

from ed_actions import EventWaits, NativeActions, _va_reader, encode_actions, parse_actor_actions, parse_ash_actions
from jpc_native_actions_case import run_case


GAME = Path(__file__).resolve().parents[1] / "game"


class EventWaitTests(unittest.TestCase):
    def test_exact_event_actor_keys_and_negative_wildcards(self):
        waits = EventWaits()
        waits.sleep(1, 4, 0, 24)
        waits.sleep(2, -1, -1, 24)
        waits.sleep(3, 4, -1, 26)
        self.assertEqual(waits.deliver(4, 1, 24), [2])
        self.assertEqual(waits.deliver(4, 0, 34), [])
        self.assertEqual(waits.deliver(4, 0, 24), [1])
        self.assertEqual(waits.deliver(4, 10, 26), [3])
        self.assertEqual(waits.waiting, {})

    def test_duplicate_sleep_and_malformed_pe_are_rejected(self):
        waits = EventWaits()
        waits.sleep(1, 0, 0, 26)
        with self.assertRaises(ValueError):
            waits.sleep(1, 0, 0, 26)
        with self.assertRaises(ValueError):
            _va_reader(b"MZ" + bytes(10))


class ActionLinkTests(unittest.TestCase):
    def test_clear_layers_skips_inactive_pointer_to_reused_waiting_child(self):
        def state(transitions,mode=0,end_event=0,begin_event=0):
            return dict(animation_id=1,start=0,loop_start=0,end=1,loop_end=1,early_end=1,finish_start=1,
                        clip_flags=0,playback_mode=mode,begin_event=begin_event,end_event=end_event,transitions=transitions)
        actor=NativeActions([state([[7,0,1],[8,1,2]]),state([[11,6,2]],end_event=31),
                             state([[10,0,0xfffe]],mode=1,begin_event=11)])
        # Explicit bounded-pool fixture, not an earned story state.
        for _ in range(24):actor._create(0,2,False)
        self.assertTrue(actor.command(8));self.assertEqual(actor.layers[6].playback,25)
        actor.command(10);self.assertIsNone(actor.layers[6].state)
        self.assertTrue(actor.command(7));self.assertEqual(actor.playbacks[25].parent,0)
        self.assertTrue(actor.command(11));self.assertEqual(actor.layers[0].playback,25)
        self.assertEqual(actor.playbacks[25].parent,0)
        self.assertEqual(actor.events[-2][1],31)


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class OriginalActionsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.states = parse_ash_actions((GAME / "EvilDead.exe").read_bytes(), (GAME / "data/az01.pc").read_bytes())

    def test_source_table_and_binary_contract(self):
        self.assertEqual(len(self.states), 163)
        encoded = encode_actions(self.states)
        self.assertEqual(encoded[:8], b"EDACT1\0\0")
        self.assertEqual(struct.unpack_from("<I", encoded, 8)[0], 163)
        self.assertEqual(len(encoded), 14027)
        self.assertEqual(self.states[147]["animation_id"], 0x01824784)
        self.assertEqual(self.states[147]["frame_count"], 29)
        self.assertEqual(self.states[147]["end_event"], 34)

    def test_use_and_hit_have_source_completion_counts_and_pop(self):
        for action, state, frames, event in ((115, 146, 35, 34), (116, 147, 29, 34),
                                            (117, 148, 17, 34), (35, 133, 12, 26), (34, 132, 15, 26)):
            with self.subTest(action=action):
                actor = NativeActions(self.states)
                self.assertTrue(actor.command(action))
                self.assertTrue(any(layer.state == state for layer in actor.layers))
                for _ in range(frames - 1):
                    actor.tick()
                    self.assertEqual(actor.events, [])
                actor.tick()
                self.assertEqual([e[1] for e in actor.events], [event])
                self.assertEqual(actor.events[0][0], frames)
                self.assertEqual(actor.layers[0].state, 0)
                self.assertTrue(all(layer.state is None for layer in actor.layers[1:]))

    def test_walk_transition_tail_and_death_override(self):
        actor = NativeActions(self.states)
        actor.command(3)
        for _ in range(5):
            actor.tick()
        self.assertEqual(actor.layers[0].state, 11)
        self.assertFalse(actor.possible(116))
        actor.command(4)
        successor = actor.playbacks[actor.layers[0].playback]
        self.assertEqual(successor.mode, 4)
        self.assertIsNotNone(successor.parent)
        actor.command(35)
        self.assertEqual(actor.layers[6].state, 133)
        actor.command(64)
        self.assertEqual(actor.layers[0].state, 162)
        self.assertTrue(all(layer.state is None for layer in actor.layers[1:]))
        for _ in range(300):
            actor.tick()
        self.assertEqual(actor.playbacks[actor.layers[0].playback].frame, 138)
        self.assertFalse(actor.possible(116))
        self.assertFalse(actor.command(255))

    def test_real_jpc_accepted_hit_stops_at_original_native_wait(self):
        result = run_case(GAME)
        self.assertEqual(result["accepted_hit"]["health"], 50)
        self.assertEqual(result["accepted_hit"]["wait"], [1, 0, 26])
        self.assertFalse(result["accepted_hit"]["resume_implemented_here"])

    def test_deadite_birth_hit_and_loop_entry_death_completion(self):
        states = parse_actor_actions((GAME / "EvilDead.exe").read_bytes(), (GAME / "data/az01.pc").read_bytes(), "deadite")
        self.assertEqual(len(states), 30)
        self.assertEqual(len(encode_actions(states)), 2170)
        actor = NativeActions(states)
        self.assertFalse(actor.possible(46))
        self.assertTrue(actor.command(70))
        for _ in range(100):
            actor.tick()
            if actor.layers[0].state == 1:
                break
        self.assertEqual([e[1] for e in actor.events], [30])
        actor.events.clear()
        self.assertTrue(actor.command(41))
        for _ in range(9):
            actor.tick()
        self.assertEqual([e[1] for e in actor.events], [26])
        self.assertTrue(actor.command(64))
        for _ in range(100):
            actor.tick()
        self.assertIn(24, [e[1] for e in actor.events])
        self.assertEqual(actor.layers[0].state, 22)

    def test_skeleton_original_birth_attack_and_death(self):
        states=parse_actor_actions((GAME/"EvilDead.exe").read_bytes(),(GAME/"data/al02.pc").read_bytes(),"skeleton")
        self.assertEqual(len(states),37)
        self.assertTrue(all(state["bone_count"]==16 for state in states))
        self.assertEqual((states[15]["animation_id"],states[15]["end_event"]),(0x07C94F04,28))
        self.assertEqual((states[28]["animation_id"],states[28]["frame_count"],states[28]["end_event"]),(0x07C95404,30,24))
        actor=NativeActions(states)
        self.assertFalse(actor.command(46))
        self.assertTrue(actor.command(70))
        for _ in range(100):
            actor.tick()
            if actor.layers[0].state==3:break
        self.assertIn(30,[event[1] for event in actor.events])
        self.assertEqual(actor.layers[0].state,3)
        self.assertTrue(actor.command(46))
        for _ in range(100):actor.tick()
        self.assertIn(28,[event[1] for event in actor.events])
        self.assertTrue(actor.command(64))
        for _ in range(100):actor.tick()
        self.assertEqual(actor.layers[0].state,28)
        self.assertIn(24,[event[1] for event in actor.events])

    def test_ah05_original_tables_attack_death_and_actual_birth_support(self):
        for name,count,bones,attack,death in (("hellbillyfat",17,16,4,11),("hellbillythin",27,16,6,14),("demonpig",17,25,11,16)):
            with self.subTest(actor=name):
                states=parse_actor_actions((GAME/"EvilDead.exe").read_bytes(),(GAME/"data/ah05.pc").read_bytes(),name)
                self.assertEqual(len(states),count)
                self.assertTrue(all(state["bone_count"]==bones for state in states))
                actor=NativeActions(states)
                self.assertFalse(actor.command(70)) # No invented common birth transition at state0.
                self.assertTrue(actor.command(46))
                self.assertEqual(actor.layers[0].state,attack)
                for _ in range(100):actor.tick()
                self.assertIn(28,[event[1] for event in actor.events])
                self.assertTrue(actor.command(64))
                for _ in range(150):actor.tick()
                self.assertEqual(actor.layers[0].state,death)
                self.assertIn(24,[event[1] for event in actor.events])
                if name=="demonpig":
                    pig=NativeActions(states);self.assertTrue(pig.command(68));self.assertTrue(pig.command(70))
                    for _ in range(100):pig.tick()
                    self.assertIn(30,[event[1] for event in pig.events])

    def test_headless_skeleton_distinct_original_birth_attack_death(self):
        states=parse_actor_actions((GAME/"EvilDead.exe").read_bytes(),(GAME/"data/ak09.pc").read_bytes(),"headlessskeleton")
        self.assertEqual(len(states),30)
        self.assertTrue(all(state["bone_count"]==16 for state in states))
        self.assertEqual((states[0]["animation_id"],states[0]["frame_count"]),(0x07883684,22))
        actor=NativeActions(states);self.assertFalse(actor.command(46));self.assertTrue(actor.command(70))
        for _ in range(100):actor.tick()
        self.assertEqual(actor.layers[0].state,2)
        self.assertIn(30,[event[1] for event in actor.events])
        self.assertTrue(actor.command(46));self.assertEqual(actor.layers[0].state,10)
        for _ in range(100):actor.tick()
        self.assertIn(28,[event[1] for event in actor.events])
        self.assertTrue(actor.command(64));self.assertEqual(actor.layers[0].state,22)
        for _ in range(100):actor.tick()
        self.assertIn(24,[event[1] for event in actor.events])

    def test_troop_original_tables_attacks_and_death(self):
        for name,count,attack,death in (("troopknife",23,10,16),("troopclaw",23,10,16),("trooprange",21,8,14)):
            with self.subTest(actor=name):
                states=parse_actor_actions((GAME/"EvilDead.exe").read_bytes(),(GAME/"data/ac01.pc").read_bytes(),name)
                self.assertEqual(len(states),count)
                self.assertTrue(all(state["bone_count"]==16 for state in states))
                actor=NativeActions(states);self.assertFalse(actor.command(70))
                self.assertTrue(actor.command(54));self.assertEqual(actor.layers[0].state,attack)
                for _ in range(130):actor.tick()
                self.assertIn(28,[event[1] for event in actor.events])
                self.assertTrue(actor.command(64));self.assertEqual(actor.layers[0].state,death)
                for _ in range(130):actor.tick()
                self.assertIn(24,[event[1] for event in actor.events])
                if name=="trooprange":self.assertEqual(states[8]["animation_id"],0x07B011C4)

    def test_tree_pair_original_states_and_mapped_45_bone_mask(self):
        for name,count,bones,attacks,death in (("treeboss",6,45,((46,1),(48,2),(50,3)),5),("treearm",3,4,((46,1),),2)):
            states=parse_actor_actions((GAME/"EvilDead.exe").read_bytes(),(GAME/"data/at02.pc").read_bytes(),name)
            self.assertEqual(len(states),count)
            self.assertTrue(all(state["bone_count"]==bones for state in states))
            for action,index in attacks:
                actor=NativeActions(states);self.assertTrue(actor.command(action));self.assertEqual(actor.layers[0].state,index)
                for _ in range(120):actor.tick()
                self.assertIn(28,[event[1] for event in actor.events])
            actor=NativeActions(states);self.assertTrue(actor.command(66));self.assertEqual(actor.layers[0].state,death)
            for _ in range(150):actor.tick()
            self.assertIn(24,[event[1] for event in actor.events])
            if name=="treeboss":
                self.assertEqual(states[0]["bone_mask"][:17],[1]*17)
                self.assertEqual(states[0]["bone_mask"][17:25],[0,0,0,46,0,1,0,48]) # Native41A680 reads all45 mapped bytes.

    def test_troop_leader_original_actions_hurt_and_death(self):
        states=parse_actor_actions((GAME/"EvilDead.exe").read_bytes(),(GAME/"data/ac05.pc").read_bytes(),"troopleaderboss")
        self.assertEqual(len(states),22)
        self.assertTrue(all(state["bone_count"]==18 for state in states))
        for action,index,end in ((137,2,34),(138,4,34),(46,12,28),(48,13,28),(41,20,26),(64,21,24)):
            actor=NativeActions(states);self.assertTrue(actor.command(action));self.assertIn(index,[layer.state for layer in actor.layers])
            for _ in range(180):actor.tick()
            self.assertIn(end,[event[1] for event in actor.events])

    def test_remaining_levela_boss_state_tables(self):
        for name,scene,count,bones in (("matriarchboss","ah06",19,32),("deadash","ag03",31,25),("anniezombie","au04",22,57)):
            states=parse_actor_actions((GAME/"EvilDead.exe").read_bytes(),(GAME/("data/"+scene+".pc")).read_bytes(),name)
            self.assertEqual(len(states),count);self.assertTrue(all(s["bone_count"]==bones for s in states))
            actor=NativeActions(states)
            if name=="matriarchboss":
                self.assertFalse(actor.command(44));self.assertTrue(actor.command(136));self.assertEqual(actor.layers[0].state,1)
                self.assertTrue(actor.command(44));self.assertIn(10,[layer.state for layer in actor.layers])
                self.assertTrue(actor.command(64));self.assertEqual(actor.layers[0].state,18)
            elif name=="deadash":
                self.assertTrue(actor.command(46));self.assertEqual(actor.layers[0].state,7)
                for _ in range(100):actor.tick()
                self.assertTrue(actor.command(64));self.assertEqual(actor.layers[0].state,23)
            else:
                self.assertFalse(actor.command(64));self.assertTrue(actor.command(136));self.assertEqual(actor.layers[0].state,1)
                for _ in range(150):actor.tick()
                self.assertEqual(actor.layers[0].state,2);self.assertTrue(actor.command(46));self.assertEqual(actor.layers[0].state,12)
                self.assertFalse(any(state["end_event"]==24 for state in states));continue
            for _ in range(200):actor.tick()
            self.assertIn(24,[event[1] for event in actor.events])


if __name__ == "__main__":
    unittest.main()
