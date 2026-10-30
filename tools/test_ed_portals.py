import math
import struct
import unittest

from ed_portals import active_portals, actor_in_portal, parse_portals, resolve_transition, scene_name


AK01 = 0x616B0100
AK09 = 0x616B0900


def make_scene():
    portal_offset = 152
    camera_offset = portal_offset + 2 * 72
    marker_offset = camera_offset + 3 * 120
    data = bytearray(marker_offset + 24)
    struct.pack_into("<4I", data, 0, 64, 88, 0, portal_offset)
    struct.pack_into("<II", data, 0x28, 2, portal_offset)
    struct.pack_into("<II", data, 0x40, 3, camera_offset)
    struct.pack_into("<II", data, 0x48, 2, marker_offset)
    struct.pack_into("<9f", data, 88, -10, -1, -10, 10, 1, 10, 1, 20, 20)
    for index in range(2):
        offset = portal_offset + index * 72
        struct.pack_into("<4B6f", data, offset, index, 0, 1, 0, -2, 0, -2, 2, 0, 2)
        struct.pack_into("<2I3f", data, offset + 52, 0xFFFFFFFF, AK09, 0, math.pi, 0)
    for index in range(3):
        offset = camera_offset + index * 120
        data[offset] = index
        struct.pack_into("<I", data, offset + 0x54, 1)
        struct.pack_into("<7f", data, offset + 92, 0, 0, 0, 0, 0, 0, 1)
    struct.pack_into("<6f", data, marker_offset, 3, 4, 5, 6, 7, 8)
    return data


class PortalTests(unittest.TestCase):
    def test_scene_identifiers_use_decimal_numbers(self):
        self.assertEqual(scene_name(AK09), "ak09")
        self.assertEqual(scene_name(0x61623200), "ab50")
        with self.assertRaises(ValueError):
            scene_name(AK09 | 1)

    def test_portal_geometry_and_enable_are_separate(self):
        data = parse_portals(make_scene(), AK01)
        self.assertEqual([p["kind"] for p in data["portals"]], ["transition", "preload"])
        self.assertTrue(actor_in_portal(data["portals"][0], [2, 99, -2]))
        self.assertEqual(active_portals(data, [0, 0, 0], []), [])
        self.assertEqual([p["index"] for p in active_portals(data, [0, 0, 0], [1])], [1])
        self.assertEqual([p["index"] for p in active_portals(data, [0, 0, 0], [1, 0])], [0, 1])

    def test_destination_marker_rotation_and_native_initial_height(self):
        source = parse_portals(make_scene(), AK01)
        destination = parse_portals(make_scene(), AK09)
        result = resolve_transition(source["portals"][0], destination)
        self.assertEqual(result["marker_xyz"], [3, 4, 5])
        self.assertEqual(result["spawn_xyz"], [3, 0, 5])
        self.assertEqual(result["camera_index"], 1)
        self.assertAlmostEqual(result["yaw"], math.pi, places=6)
        with self.assertRaises(ValueError):
            resolve_transition(source["portals"][1], destination)
        with self.assertRaises(ValueError):
            resolve_transition(source["portals"][0], source)

    def test_rejects_missing_targets_and_corrupt_tables(self):
        source = parse_portals(make_scene(), AK01)
        destination = parse_portals(make_scene(), AK09)
        portal = dict(source["portals"][0], marker_index=255)
        with self.assertRaises(ValueError):
            resolve_transition(portal, destination)
        portal = dict(source["portals"][0], camera_index=99)
        with self.assertRaises(ValueError):
            resolve_transition(portal, destination)
        for offset, value in [(0x2C, 999999), (152 + 4, math.nan)]:
            data = make_scene()
            struct.pack_into("<f" if isinstance(value, float) else "<I", data, offset, value)
            with self.assertRaises(ValueError):
                parse_portals(data, AK01)


if __name__ == "__main__":
    unittest.main()
