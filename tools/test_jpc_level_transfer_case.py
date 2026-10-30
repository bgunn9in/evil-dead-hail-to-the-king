"""Read-only regression against the user's original, separately loaded packages."""

from pathlib import Path
import unittest

from jpc_level_transfer_case import PlayerTransferCase, full_save_case, run_case, normalized_code, native_transfer_policy


GAME = Path(__file__).resolve().parents[1] / "game"


class NativeBoundaryPredicateTests(unittest.TestCase):
    def test_exact_entry_gates_not_arbitrary_cross_package_restore(self):
        self.assertTrue(native_transfer_policy(0x61750400, 0x626D0100)["restore_player"])
        self.assertTrue(native_transfer_policy(0x62620100, 0x616B0100)["restore_player"])
        self.assertFalse(native_transfer_policy(0x626D0100, 0x616B0100)["restore_player"])
        self.assertFalse(native_transfer_policy(0x61750400, 0x626D0101)["restore_player"])
        self.assertFalse(native_transfer_policy(0x616B0100, 0x616B0900)["reload_package"])
        restart = native_transfer_policy(0x626D0100, 0x62620101)
        self.assertTrue(restart["reload_package"])
        self.assertTrue(restart["clear_transfer_buffer"])
        self.assertFalse(restart["save_player"])


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class OriginalPackageTransferTests(unittest.TestCase):
    def test_separate_vms_original_player_stream_and_bit_oracle(self):
        report = run_case(GAME)
        self.assertEqual(len(report["transfer_cases"]), 4)
        for case in report["transfer_cases"]:
            self.assertEqual(case["partial_bits"], 3)
            self.assertEqual(case["full_bytes_cursor"], 446 if case["equipped"] else 444)
            self.assertEqual(case["restored"]["health"], 73)
            self.assertEqual(case["restored"]["mission_flag435"], 0)
            self.assertIn(814, case["source_methods"])
            self.assertIn(822, case["target_methods"])
            self.assertEqual(case["player_sha256"],
                             "6333c4e24416c635cfc824a93d52b9c3f3c0b2d80bad1be6e8f59f93023bc158" if case["equipped"] else
                             "8d6a0dafc730a17f7ca7bbf6527163253b607c560cf70059bc2721de9890ba7f")

    def test_transfer_kernel_ids_and_resolved_bodies_match(self):
        a, b = PlayerTransferCase(GAME, "A"), PlayerTransferCase(GAME, "B")
        for method_index in (69, 123, 155, 164, 176, 193, 199, 221, 223, 644, 655,
                             663, 678, 682, 814, 822, 1031, 1041, 1301, 1314, 1517, 1588):
            left, right = a.package.methods[method_index], b.package.methods[method_index]
            self.assertEqual((left["class_name"], left["name"], left["descriptor"]),
                             (right["class_name"], right["name"], right["descriptor"]))
            self.assertEqual(normalized_code(a.package, left), normalized_code(b.package, right))
        self.assertNotEqual(a.vm.method_index("Deadite", "initialize"), b.vm.method_index("Deadite", "initialize"))

    def test_full_save_is_tagged_and_retains_original_unaligned_copy(self):
        for tag in "AB":
            result = full_save_case(GAME, tag)
            self.assertTrue(result["opposite_package_rejected"])
            self.assertTrue(result["quest_flag435_restored"])
            self.assertEqual(result["unaligned_source_copy"]["restored_last_ammo"], 61)
            self.assertEqual(result["restore_return_bytes"], result["save_return_bytes"] + 1)


if __name__ == "__main__":
    unittest.main()
