from pathlib import Path
import unittest

from jpc_levela_scene_probe import LevelASceneProbe, run_probe
from export_episode import SCENES


GAME = Path(__file__).resolve().parents[1] / "game"


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class LevelASceneProbeTests(unittest.TestCase):
    def test_supported_rooms_execute_original_fresh_and_revisit_stream_paths(self):
        result = run_probe(GAME, SCENES)
        self.assertEqual(result["completed"], list(SCENES))
        self.assertFalse(result["strict_gaps"])
        for case in result["cases"]:
            self.assertEqual(len(case["stages"]), 5)
            self.assertTrue(case["saved_room_stream_sha256"])
        at01 = next(case for case in result["cases"] if case["scene"] == "at01")
        self.assertEqual(at01["fresh_waves"][3]["prototypes"][0], 16)
        self.assertFalse(at01["fresh_waves"][3]["active"])
        self.assertFalse(at01["revisit_wave_flags"][3] & 0x10000)

    def test_missing_native_is_a_precise_failure_not_success(self):
        case = LevelASceneProbe(GAME)
        del case.vm.natives[("ed/base/External", "audioPlay", "(II)V")]
        result = case.probe("at02")
        self.assertEqual(result["status"], "incomplete")
        self.assertIn("audioPlay", result["stages"][-1]["error"])
        self.assertFalse(result["stages"][-1]["completed"])

    def test_object_translation_keeps_zero_and_uses_source_float_centimeters(self):
        case = LevelASceneProbe(GAME)
        case._object_setup(0, 1, 2, 0, 3)
        state = case.object_properties[0]
        state.update(position=[1., -2., 3.], marker=4)
        case._object_translate(0, 0, 0, 0)
        self.assertEqual(state["marker"], 4)
        case._object_translate(0, 12, -34, 56)
        self.assertEqual(state["position"], [case._float(1 + case._float(12 * case._float(.01))),
                                             case._float(-2 + case._float(-34 * case._float(.01))),
                                             case._float(3 + case._float(56 * case._float(.01)))])
        self.assertEqual(state["marker"], -1)
        self.assertEqual(case._object_flags_sub(0, 1), 10)
        self.assertEqual(case._object_flags_sub(0, 1), 10)


if __name__ == "__main__":
    unittest.main()
