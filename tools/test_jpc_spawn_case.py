import unittest

from jpc_spawn_case import NativeRandom, ROOT, SpawnCase, native_distance_xz


class NativeSpawnMathTests(unittest.TestCase):
    def test_native_distance_ignores_y_and_truncates_centimeters(self):
        self.assertEqual(native_distance_xz([0, 999, 0], [0, -999, 3.009]), 300)
        self.assertEqual(native_distance_xz([0, 0, 0], [0, 0, 3.01]), 300)
        self.assertEqual(native_distance_xz([1, 99, 2], [1, -99, 2]), 100)

    def test_equal_random_bounds_do_not_advance_native_table(self):
        generator = NativeRandom()
        self.assertEqual(generator.range(9, 9), 9)
        self.assertEqual((generator.seed, generator.index, generator.ready), (12345, 0, False))
        first = generator.range(0, 100)
        self.assertEqual(first, NativeRandom().range(0, 100))
        self.assertEqual(generator.index, 1)

    def test_random_bound_uses_one_seed_step_without_touching_range_table(self):
        generator = NativeRandom()
        self.assertEqual(generator.bounded(0), 0)
        self.assertEqual(generator.seed, 12345)
        expected_seed = (12345 * 0x41C64E6D + 0x3039) & 0xFFFFFFFF
        expected = (((expected_seed * 0x88888889) >> 32) >> 3) % 0x07FFFFFF
        self.assertEqual(generator.bounded(100), expected % 100)
        self.assertEqual(generator.seed, expected_seed)
        self.assertFalse(generator.ready)
        self.assertEqual(generator.index, 0)


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class OriginalSpawnTests(unittest.TestCase):
    def test_ak09_fresh_stream_activates_original_deadite_wave(self):
        case = SpawnCase(ROOT / "game")
        wave = case.first_visit()
        self.assertEqual((wave["radius_cm"], wave["max_concurrent"], wave["interval_ms"], wave["last_spawn_ms"]),
                         (300, 2, 15000, -15000))
        self.assertEqual(wave["flags"], 0x1810A)
        selected = case.frame_until_allocation(0)
        self.assertEqual((selected["requested_actor_type"], selected["selected_marker"]), (4, 12))
        self.assertEqual(selected["allocation_caller"], [970, 277])
        self.assertIn(1001, selected["executed_methods"])

    def test_actual_arrival_requires_approach_and_original_15_frame_backoff(self):
        case = SpawnCase(ROOT / "game")
        case.first_visit()
        case.position = list(case.scene_data["markers"][2])
        self.assertIsNone(case.frame_until_allocation(0))
        self.assertEqual(case.vm.statics[777].values[4], 15)
        target = case.scene_data["markers"][12]
        case.position = [a + (b - a) * .1 for a, b in zip(case.position, target)]
        for _ in range(15):
            self.assertIsNone(case.frame_until_allocation(0))
        selected = case.frame_until_allocation(0)
        self.assertEqual(selected["selected_marker"], 12)


if __name__ == "__main__":
    unittest.main()
