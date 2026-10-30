from pathlib import Path
import unittest

from audit_levelb_actor_methods import analyze


GAME = Path(__file__).resolve().parents[1] / "game"


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class LevelBMethodTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.classes = {actor["class"]: {method["name"]: method for method in actor["methods"]}
                       for actor in analyze(GAME)["actors"]}

    def test_merchant_literal_no_damage_no_hit_no_attack(self):
        merchant = self.classes["Merchant"]
        self.assertTrue(merchant["doDamageCalculation"]["returns_zero"])
        for name in ("eventHitWeapon", "actionAttack", "actionDie"):
            self.assertTrue(merchant[name]["return_only"])

    def test_cinematic_cthulhu_and_pump_keeper_keep_distinct_source_hooks(self):
        self.assertTrue(self.classes["Cthulhu"]["actionAttack"]["return_only"])
        death = self.classes["Cthulhu"]["actionDie"]
        self.assertIn((612, 1), [(write["field"], write["adjacent_literal_value"]) for write in death["field_writes"]])
        pump = self.classes["PumpKeeper"]["eventHitWeapon"]
        self.assertIn(["GenericNpc", "eventHitWeapon", "(III)V"], [call["symbol"] for call in pump["calls"]])


if __name__ == "__main__":
    unittest.main()
