import struct
import unittest

from audit_vm_heap import census, fnv


def snapshot(frame_root=0, literal_root=0):
    data = bytearray(b"EDVMST3\0")
    def put(*items):
        for item in items:
            data.extend(struct.pack("<I", item & 0xFFFFFFFF))
    put(123, 4, 1, 0, 1, 1)  # package,count; static0 -> object1
    put(0, 0, 1, 0, 1, 2, 0)  # object1 -> object2
    put(1, 0, 0, 0)  # object2
    put(2, 0, 1, 0, 1, 4, 0)  # orphan cycle3 ->4
    put(2, 0, 1, 0, 1, 3, 0)  # orphan cycle4 ->3
    put(1 if literal_root else 0)
    if literal_root:
        put(77, literal_root)
    put(0, 0, 0, 2, 1 if frame_root else 0)  # initialized,frame64,nextThread,threadcount
    if frame_root:
        put(1, 1, 0, 1, 0, 0, 0, 100, 0)  # nine-word wait header
        put(2, 0, -1, 0, 0, 1)  # void result, initial method/ref/args, frame count
        put(0, frame_root, 1, 1)
        data.extend(b"V")
        put(0, 0, 0, 1, 0)  # args/locals/stack, flags/detached
    put(fnv(data))
    return bytes(data)


class CensusTests(unittest.TestCase):
    def test_unreachable_cycle_and_host_roots(self):
        result = census(snapshot())
        self.assertEqual((result["objects"], result["cells"], result["reachable_objects"], result["unreachable_objects"]), (4, 3, 2, 2))
        self.assertEqual(census(snapshot(), [3])["unreachable_objects"], 0)

    def test_corruption_and_bad_host_root(self):
        raw = bytearray(snapshot()); raw[17] ^= 1
        with self.assertRaises(ValueError):
            census(bytes(raw))
        with self.assertRaises(ValueError):
            census(snapshot(), [5])

    def test_literal_and_parked_frame_are_roots(self):
        self.assertEqual(census(snapshot(literal_root=3))["unreachable_objects"], 0)
        result = census(snapshot(frame_root=3))
        self.assertEqual(result["threads"], 1)
        self.assertEqual(result["unreachable_objects"], 0)


if __name__ == "__main__":
    unittest.main()
