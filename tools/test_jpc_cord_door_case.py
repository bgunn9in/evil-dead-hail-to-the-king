import math
import unittest

from jpc_cord_door_case import ROOT, CordDoorCase, run_case


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class CordDoorTests(unittest.TestCase):
    def test_original_pickup_use_consumption_and_delayed_passage(self):
        result = run_case(ROOT / "game")
        self.assertEqual(result["remaining_inventory"], [])
        self.assertEqual(result["mission_field432"], 1)
        self.assertEqual(result["mission_field435"], 1)
        self.assertEqual(result["source_scene_countdown1616"], 90)
        self.assertIn(1940, result["use_methods"])
        self.assertIn(1945, result["use_methods"])

    def test_no_cord_cannot_request_the_passage(self):
        case = CordDoorCase(ROOT / "game")
        case.enter_machinery()
        self.assertEqual(case.apply_cord(), 0)
        self.assertEqual(case.vm.statics[435], 0)
        self.assertFalse(case.passage_ready())

    def test_source_facing_and_position_preserve_cord_on_failure(self):
        for wrong_facing in (True, False):
            with self.subTest(wrong_facing=wrong_facing):
                case = CordDoorCase(ROOT / "game")
                case.pickup_cord()
                case.enter_machinery()
                if wrong_facing:
                    case.yaw += math.pi
                else:
                    case.position[0] += 10
                self.assertEqual(case.apply_cord(), 0)
                self.assertEqual(case.items()[0]["item"], 29)
                self.assertEqual(case.items()[0]["quantity"], 1)
                self.assertEqual(case.vm.statics[435], 0)
                self.assertFalse(case.passage_ready())

    def test_original_voice_still_playing_forces_enable_at45_ticks(self):
        case = CordDoorCase(ROOT / "game")
        case.pickup_cord()
        case.enter_machinery()
        case.voice_done = False
        self.assertEqual(case.apply_cord(), 1)
        for tick in range(45):
            case.sound_tick()
            self.assertEqual(case.passage_ready(), tick == 44)


if __name__ == "__main__":
    unittest.main()
