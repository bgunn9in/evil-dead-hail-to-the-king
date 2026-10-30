from pathlib import Path
import unittest

from audit_movie_flags import audit


GAME = Path(__file__).resolve().parents[1] / "game"


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class OriginalMovieFlagsTests(unittest.TestCase):
    def test_all_authored_calls_resolve_without_runtime_guessing(self):
        result = audit(GAME)
        self.assertEqual(result["packages"]["A"]["flag_counts"], {0: 24, 2: 1, 4: 1})
        self.assertEqual(result["packages"]["B"]["flag_counts"], {0: 29, 2: 3, 4: 1})
        nonzero = {(r["owner"], r["pc"], r["flags"])
                   for p in result["packages"].values() for r in p["calls"] if r["flags"]}
        self.assertEqual(nonzero, {("SceneAA01", 43, 4), ("SceneAT01", 361, 2),
                                   ("SceneBC02", 899, 2), ("SceneBG12", 35, 2),
                                   ("SceneBM02", 28, 4), ("SceneBT11", 30, 2)})


if __name__ == "__main__":
    unittest.main()
