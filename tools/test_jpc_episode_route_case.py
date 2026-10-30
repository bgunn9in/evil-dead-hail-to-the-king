import math
import unittest

from ed_triggers import contains
from export_episode import load_contexts
from jpc_episode_route_case import ROOT, run_case, segment_distance


class RouteGeometryTests(unittest.TestCase):
    def test_exact_segments_cross_overlap_and_clearance(self):
        self.assertEqual(segment_distance([0, 0], [2, 0], [1, -1], [1, 1]), 0)
        self.assertEqual(segment_distance([0, 0], [2, 0], [1, 0], [3, 0]), 0)
        self.assertAlmostEqual(segment_distance([0, 0], [2, 0], [0, 1], [2, 1]), 1)


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class OriginalEpisodeRouteTests(unittest.TestCase):
    def test_cord_episode_reaches_original_passages_with_static_clearance(self):
        result = run_case(ROOT / "game")
        contexts, _ = load_contexts(ROOT / "game")
        self.assertEqual(len(result["legs"]), 9)
        self.assertLess(result["startup_clearance_note"]["clearance"], result["actor_radius"])
        for leg in result["legs"]:
            with self.subTest(scene=leg["scene"], purpose=leg["purpose"]):
                self.assertGreaterEqual(leg["minimum_static_clearance"]+1e-6, .25)
                self.assertGreater(leg["length_world_units"], 0)
                self.assertEqual(leg["active_non_target_portal_intersections"], [])
                self.assertTrue(all(math.isfinite(v) for p in leg["waypoints"] for v in p["position_xyz"]))
                if leg["portal_index"] is not None:
                    passage = next(p for p in contexts[leg["scene"]]["portals"]["portals"]
                                   if p["index"] == leg["portal_index"])
                    self.assertTrue(contains(passage, leg["waypoints"][-1]["position_xyz"]))


if __name__ == "__main__":
    unittest.main()
