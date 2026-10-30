import unittest
from pathlib import Path

from ah05_passage_case import source_case, segment_clearance, saved_prefix


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class AH05PassageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.case = source_case(Path(__file__).resolve().parents[1] / "game")

    def test_original_throat_is_narrower_than_modern_diameter(self):
        self.assertAlmostEqual(self.case["rectangle52_53"]["gap_m"], .38995361328125)
        self.assertAlmostEqual(self.case["entrance42_43"]["gap_m"], .3524501171966958)
        self.assertAlmostEqual(self.case["entrance42_43"]["radius_upper_bound_m"], .1762250585983479)
        self.assertFalse(self.case["modern_radius_025_fits_throat"])

    def test_original_point_and_smaller_witness_pass(self):
        self.assertTrue(self.case["original_nav18"]["point_clear"])
        self.assertGreater(self.case["original_nav18"]["static_clearance_m"], 0)
        self.assertLess(self.case["original_nav18"]["static_clearance_m"], .25)
        self.assertTrue(self.case["vertical_point_witness"]["point_clear"])
        self.assertTrue(self.case["vertical_point_witness"]["radius_017_clear"])

    def test_collision_class_does_not_exclude_the_barrels(self):
        for volume in self.case["volumes"].values():
            self.assertEqual(volume["flags_raw"], 0xffffffff)

    def test_witness_does_not_use_an_intersecting_point_segment(self):
        rectangle = [(0, 0), (0, 1), (1, 1), (1, 0)]
        self.assertEqual(segment_clearance((-1, .5), (2, .5), [rectangle]), 0)
        self.assertEqual(segment_clearance((.5, .5), (2, .5), [rectangle]), 0)
        self.assertAlmostEqual(segment_clearance((-1, -1), (2, -1), [rectangle]), 1)


class SavedPrefixTests(unittest.TestCase):
    def test_truncated_saved_prefix_rejected(self):
        with self.assertRaises(ValueError):
            saved_prefix(b"\x07\0\0\0EDPLAY1")

    def test_unknown_header_rejected(self):
        with self.assertRaises(ValueError):
            saved_prefix(b"\x05\0\0\0wrong")


if __name__ == "__main__":
    unittest.main()
