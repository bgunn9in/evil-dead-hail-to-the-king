import unittest
from pathlib import Path

from ed_scripts import ScriptPackage
from jpc_actor_event_case import AshHitCase


SCRIPT = Path(__file__).resolve().parents[1] / "game/data/script/pc/levelA.jpc"


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class OriginalActorEventTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.package = ScriptPackage(SCRIPT.read_bytes())

    def test_nonlethal_source_event_updates_health_portrait_and_requests_reaction(self):
        case = AshHitCase(self.package, health=70)
        self.assertEqual(case.hit(20), 50)
        self.assertEqual(case.ash.fields[130], 1234)
        self.assertEqual(case.gui_values[(11, 0)], 50)
        self.assertEqual(case.gui_colors[11], (255, 255, 0, 255))
        self.assertEqual(case.face.fields[380], 60)
        self.assertEqual(case.face.fields[840], 0x00030003)
        self.assertEqual(case.face.fields[841], 4)
        self.assertEqual(case.ash.fields[8], 35)
        self.assertEqual(case.ash.fields[114], 0)
        self.assertIn(("actorDoConsoleAction", (1, 0, 35)), case.calls)
        self.assertEqual(case.last_dispatch_result, 1)
        self.assertIn((649, 514), case.vm.trace)
        self.assertIn((703, 20), case.vm.trace)
        self.assertEqual(case.vm.statics[179], 1)
        self.assertEqual(case.vm.statics[181].values[0], 36)
        self.assertEqual(case.vm.statics[184].values[0], 20)
        self.assertIs(case.vm.statics[180].values[0], case.ash)
        self.assertEqual(case.ash.fields[11], 0)
        self.assertEqual(case.death_reaction(), -1)

    def test_lethal_source_event_clamps_zero_without_hit_action(self):
        case = AshHitCase(self.package, health=12)
        self.assertEqual(case.hit(20), 0)
        self.assertEqual(case.gui_values[(11, 0)], 100)
        self.assertEqual(case.gui_colors[11], (255, 0, 0, 255))
        self.assertFalse(any(name == "actorDoConsoleAction" for name, _ in case.calls))
        self.assertEqual(case.death_reaction(), 32)

    def test_guard_skips_all_hit_effects_and_damage_suppression_only_skips_subtraction(self):
        guarded = AshHitCase(self.package, health=70, guard=1)
        self.assertEqual(guarded.hit(20), 70)
        self.assertEqual(guarded.ash.fields[130], 0)
        self.assertEqual(guarded.calls, [])
        suppressed = AshHitCase(self.package, health=70, damage_suppressed=1)
        self.assertEqual(suppressed.hit(20), 70)
        self.assertEqual(suppressed.ash.fields[130], 1234)
        self.assertTrue(suppressed.calls)

    def test_all_source_reaction_codes_and_extended_state_clear(self):
        for attack, expected in ((36, 35), (37, 34), (38, 32), (39, 33),
                                 (40, 36), (41, 37), (42, 38)):
            with self.subTest(attack=attack):
                case = AshHitCase(self.package)
                reset_fields = (121, 122, 123, 124, 96, 1256, 1257)
                case.ash.fields.update(dict.fromkeys(reset_fields, 7))
                case.hit(1, attack_type=attack)
                self.assertIn(("actorDoConsoleAction", (1, 0, expected)), case.calls)
                if attack in (40, 42):
                    self.assertEqual([case.ash.fields[i] for i in reset_fields], [0] * 7)
                    # ActorDB unwinds the callback nesting count after the override returns.
                    self.assertEqual(case.ash.fields[11], 0)

    def test_native_state_refusal_runs_original_vfx_and_sound_wrappers(self):
        case = AshHitCase(self.package, transition_possible=False)
        case.hit(1, attack_type=40)
        self.assertIn(("vfxSpawnActorJoint", (2, 1, 0, 1, 0, 0, 0)), case.calls)
        self.assertIn(("sndMgrGetSNDID", (65549,)), case.calls)
        self.assertIn(("sndMgrPlaySoundVol", (17, 100)), case.calls)
        self.assertFalse(any(name == "actorDoConsoleAction" for name, _ in case.calls))

    def test_portrait_not_at_home_executes_original_slide_fields(self):
        case = AshHitCase(self.package, at_home=False)
        case.hit(1)
        self.assertEqual(case.gui_flags[7], 1)
        self.assertEqual(case.face.fields[380], 60)
        self.assertEqual(case.face.fields[841] & 2, 2)
        self.assertNotIn("random", [name for name, _ in case.calls])


if __name__ == "__main__":
    unittest.main()
