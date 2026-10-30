import math
import unittest

from export_scene import ROOT
from jpc_pickup_case import PickupCase, run_case


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class OriginalPickupTests(unittest.TestCase):
    def test_original_pickup_repeat_and_capacity_failure(self):
        result = run_case(ROOT / "game")
        self.assertEqual(result["action"], 116)
        self.assertIn(2033, result["executed_methods_on_pickup"])
        self.assertTrue(result["repeat_empty_location_verified"])
        self.assertTrue(result["capacity_failure_verified"])

    def test_original_zone_and_facing_prevent_pickup(self):
        case = PickupCase(ROOT / "game")
        case.yaw += math.pi
        self.assertEqual(case.interact(), 0)
        self.assertEqual(case.items(), [])
        self.assertEqual(case.vm.statics[418], 0)
        case.position = list(case.scene_data["navpoints"][23])
        self.assertEqual(case.interact(), 0)
        self.assertEqual(case.items(), [])
        self.assertEqual(case.vm.statics[418], 0)

    def test_original_stack_increases_quantity_in_existing_slot(self):
        case = PickupCase(ROOT / "game")
        self.assertEqual(case.vm.invoke(1310, case.inventory, (119, 2)), 0)
        self.assertEqual(case.interact(), 116)
        self.assertEqual(case.items(), [{"slot": 0, "item": 119, "quantity": 3, "flags": 60}])
        self.assertIn(1321, {method for method, _ in case.vm.trace})


if __name__ == "__main__":
    unittest.main()
