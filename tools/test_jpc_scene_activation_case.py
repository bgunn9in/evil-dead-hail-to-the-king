import unittest

from jpc_scene_activation_case import ROOT, SCENES, SceneActivationCase


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class SceneActivationTests(unittest.TestCase):
    def test_three_adjacent_scenes_execute_original_activation(self):
        for name, disabled in (("ab50", []), ("ac51", [2, 5]), ("al50", [])):
            with self.subTest(scene=name):
                result = SceneActivationCase(ROOT / "game").activate_arrival(name)
                self.assertIn(result["activation_method"], result["executed_methods"])
                self.assertFalse(result["activation_fresh_argument"])
                self.assertEqual([i for i, on in result["portal_enabled"].items() if not on], disabled)

    def test_original_arrival_activation_all_eight_scenes(self):
        for name in SCENES:
            with self.subTest(scene=name):
                result = SceneActivationCase(ROOT / "game").activate_arrival(name)
                self.assertFalse(result["activation_fresh_argument"])
                self.assertIn(result["activation_method"], result["executed_methods"])
                self.assertGreater(result["steps"], 400)

    def test_source_machinery_phase_and_progression_portals(self):
        for progression in (0, 1):
            for used in (0, 1):
                with self.subTest(progression=progression, used=used):
                    fields = {405: progression, 435: used}
                    first = SceneActivationCase(ROOT / "game").activate_arrival("ah50", fields)
                    self.assertEqual([i for i, on in first["portal_enabled"].items() if not on], [3 if used else 4])
                    second = SceneActivationCase(ROOT / "game").activate_arrival("ah51", fields)
                    expected = sorted(([0, 4] if used else [1, 5]) + ([2, 6] if progression else [3, 7]))
                    self.assertEqual([i for i, on in second["portal_enabled"].items() if not on], expected)

    def test_ah02_sphere_active_and_intro_runs_only_once(self):
        case = SceneActivationCase(ROOT / "game")
        first = case.activate_arrival("ah02", {435: 1})
        self.assertTrue(case.volume_enabled[33])
        self.assertEqual(first["disabled_volumes"], [])
        self.assertTrue(all(first["portal_enabled"].values()))
        self.assertEqual(first["state_changes"][436], 1)
        self.assertEqual([item["args"] for item in first["state_calls"] if item["native"] == "moviePlay"], [[0x61303600, 0]])
        second = case.activate_arrival("ah02")
        self.assertTrue(case.volume_enabled[33])
        self.assertFalse(any(item["native"] == "moviePlay" for item in second["state_calls"]))
        self.assertNotIn(436, second["state_changes"])

    def test_ah01_keeps_item_passage_closed_until_original_use_callback(self):
        for used in (0, 1):
            result = SceneActivationCase(ROOT / "game").activate_arrival("ah01", {435: used})
            self.assertFalse(result["portal_enabled"][3])
            self.assertEqual(result["disabled_volumes"], [51])


if __name__ == "__main__":
    unittest.main()
