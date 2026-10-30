from pathlib import Path
import unittest

from audit_screen_effects import audit


GAME = Path(__file__).resolve().parents[1] / "game"


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class OriginalScreenEffectsTests(unittest.TestCase):
    def test_actual_native_slice_and_all_source_fade_calls(self):
        result = audit(GAME)
        self.assertEqual(len(result["fade"]["out_alpha_golden"]), 32)
        self.assertEqual(len(result["fade"]["in_alpha_golden"]), 16)
        self.assertEqual(result["fade"]["out_alpha_golden"][-2:], [248, 255])
        self.assertEqual(result["fade"]["in_alpha_golden"][-2:], [15, 0])
        for tag in "AB":
            fades = [r for r in result["packages"][tag]["calls"] if r["native"] != "scrFxShake"]
            self.assertEqual([(r["method"], r["pc"], r["native"]) for r in fades],
                             [(1672, 78, "scrFxFadeOut"), (1672, 81, "scrFxComplete"), (1672, 100, "scrFxFadeIn")])


if __name__ == "__main__":
    unittest.main()
