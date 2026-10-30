from pathlib import Path
import unittest

from native_frame_clock import FrameGate, audit


class FrameGateTests(unittest.TestCase):
    def test_threshold_and_slow_frame_does_not_catch_up(self):
        gate = FrameGate(0)
        self.assertFalse(gate.offer(32))
        self.assertTrue(gate.offer(33))
        self.assertFalse(gate.offer(65))
        self.assertTrue(gate.offer(200))
        self.assertEqual(gate.last, 200)
        self.assertFalse(gate.offer(200))
        self.assertFalse(gate.offer(232))
        self.assertTrue(gate.offer(233))

    def test_uint32_wrap_is_native_subtract_and_unsigned_compare(self):
        gate = FrameGate(0xFFFFFFF0)
        self.assertFalse(gate.offer(0x10))
        self.assertTrue(gate.offer(0x11))
        with self.assertRaises(ValueError):
            gate.offer(-1)
        with self.assertRaises(ValueError):
            FrameGate(1 << 32)

    def test_one_second_and_authored_tick_durations(self):
        gate = FrameGate(0)
        accepted = [t for t in range(1, 1001) if gate.offer(t)]
        self.assertEqual(accepted, list(range(33, 991, 33)))
        self.assertEqual(45 * 33, 1485)

    @unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
    def test_original_native_predicates(self):
        result = audit(Path(__file__).resolve().parents[1] / "game")
        self.assertEqual(result["period_ms"], 33)
        self.assertEqual([r["milliseconds"] for r in result["cpu_golden"] if r["accepted"]], [33, 66, 200, 233])


if __name__ == "__main__":
    unittest.main()
