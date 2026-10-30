import unittest

from ed_scripts import ScriptPackage
from ed_vm import ScriptError
from jpc_conditional_door_case import ROOT, describe_case, make_case, run_case


PACKAGE = ROOT / "game/data/script/pc/levelA.jpc"


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class ConditionalDoorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.package = ScriptPackage(PACKAGE.read_bytes())

    def test_original_initializer_routes_to_al01(self):
        result = run_case(self.package)
        self.assertEqual((result["initial_field_value"], result["input_field_value"], result["final_field_value"]), (0, 0, 0))
        self.assertEqual(result["portal_operations"], [[9, 0], [10, 0], [8, 0], [7, 0], [11, 1], [12, 0]])
        self.assertEqual(result["condition_trace"], [538, 541, 544, 546, 549, 551, 554])
        self.assertEqual(result["steps"], 1482)
        self.assertIn(332, result["executed_methods"])
        self.assertIn(361, result["executed_methods"])

    def test_restored_nonzero_state_routes_to_al50(self):
        for value in (1, -1):
            with self.subTest(value=value):
                result = run_case(self.package, value)
                self.assertEqual(result["initial_field_value"], 0)
                self.assertEqual(result["final_field_value"], value)
                self.assertEqual(result["portal_operations"][-2:], [[11, 0], [12, 1]])
                self.assertEqual(result["condition_trace"], [538, 541, 555, 557, 560, 562, 565])

    def test_unbound_native_is_not_silently_executed_as_a_dummy_return(self):
        vm, receiver, engine = make_case(self.package)
        del vm.natives[("ed/base/External", "scenePortalEnable", "(II)V")]
        with self.assertRaisesRegex(ScriptError, r"scenePortalEnable.*Unbound native"):
            vm.invoke(describe_case(self.package)["method_index"], receiver, (0,))
        self.assertEqual(engine["portal_operations"], [])

    def test_changed_meaningful_source_branch_is_rejected(self):
        package = ScriptPackage(PACKAGE.read_bytes())
        original = package.disassemble

        def changed(index):
            instructions = original(index)
            if index == 2042:
                instructions[-11]["mnemonic"] = "ifeq"
            return instructions

        package.disassemble = changed
        with self.assertRaisesRegex(ValueError, "verified source"):
            run_case(package)


if __name__ == "__main__":
    unittest.main()
