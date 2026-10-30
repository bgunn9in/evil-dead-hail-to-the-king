from pathlib import Path
import unittest

from jpc_at01_spawn_case import run_case


class AT01SpawnTests(unittest.TestCase):
    @unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
    def test_original_restore_configures_but_does_not_begin_actor16_wave(self):
        game = Path(__file__).resolve().parents[1] / "game"
        if not (game / "data/at01.pc").is_file():
            self.skipTest("Original user game not present")
        result = run_case(game, 100)
        self.assertEqual(result["scene_index"], 28)
        self.assertTrue({1784, 962, 986, 981, 939, 2086}.issubset(result["fresh_methods"]))
        self.assertEqual(result["wave3_type"], 16)
        self.assertEqual(result["saved_source_stream_size"], 342)
        for field in ("fresh_wave3_flags", "restored_wave3_flags", "isolated_wave3_flags"):
            self.assertEqual(result[field], 0x508)
            self.assertFalse(result[field] & result["active_bit"])
        self.assertEqual(result["isolated_in_progress"], 0)


if __name__ == "__main__":
    unittest.main()
