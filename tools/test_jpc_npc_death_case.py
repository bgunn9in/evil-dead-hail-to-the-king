import unittest

from ed_scripts import ScriptPackage
from jpc_npc_death_case import ROOT, NpcDeathCase, run_case


SCRIPT = ROOT / "game/data/script/pc/levelA.jpc"


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class NpcDeathTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.package = ScriptPackage(SCRIPT.read_bytes())

    def test_original_weapon_dispatch_damage_ai_and_native_death_request(self):
        result = run_case(self.package)
        self.assertEqual(result["health_after_hits"], [4, 0])
        self.assertIn(1006, result["first_hit_methods"])
        self.assertIn(1707, result["constructor_methods"])
        self.assertIn(1713, result["think_methods"])
        self.assertIn(1561, result["think_methods"])
        self.assertIn(1625, result["think_methods"])
        self.assertEqual((result["death_behavior"], result["native_death_action"]), (32, 64))

    def test_source_damage_halving_and_lethal_clamp(self):
        half = NpcDeathCase(self.package, half_damage=True)
        self.assertEqual(half.hit(), 8)
        lethal = NpcDeathCase(self.package, health=3)
        self.assertEqual(lethal.hit(), 0)
        self.assertEqual(lethal.think_to_death_request(), 64)

    def test_invisible_npc_without_line_of_sight_takes_no_damage(self):
        hidden = NpcDeathCase(self.package, line_of_sight=False, visible=False)
        self.assertEqual(hidden.hit(), 12)
        self.assertNotIn(1006, {method for method, _ in hidden.vm.trace})
        visible = NpcDeathCase(self.package, line_of_sight=False, visible=True)
        self.assertEqual(visible.hit(), 4)

    def test_source_distance_branch_chooses_far_death_action(self):
        case = NpcDeathCase(self.package, health=1, distance=300)
        case.hit()
        self.assertEqual(case.think_to_death_request(), 118)


if __name__ == "__main__":
    unittest.main()
