import copy
import math
import struct
import unittest
from unittest.mock import patch

from ed_doors import AK01, AK09, decode_doors
from ed_portals import parse_portals
from ed_scripts import ScriptPackage
from test_ed_scripts import fixture


SPECS = {
    AK01: [(3, 69, 28, 40, AK09, 12, 3), (2, 74, 32, 45, AK09, 8, 2)],
    AK09: [(8, 57, 51, 40, AK01, 11, 1), (7, 58, 52, 40, AK01, 3, 0)],
}


def source_scene(scene_id):
    volume_offset = 152
    portal_offset = volume_offset + 111 * 72
    camera_offset = portal_offset + 13 * 72
    marker_offset = camera_offset + 15 * 120
    data = bytearray(marker_offset + 53 * 12)
    struct.pack_into("<4I", data, 0, 64, 88, 111, volume_offset)
    struct.pack_into("<II", data, 0x28, 13, portal_offset)
    struct.pack_into("<II", data, 0x40, 15, camera_offset)
    struct.pack_into("<II", data, 0x48, 53, marker_offset)
    struct.pack_into("<9f", data, 88, 0, -1, 0, 1000, 1, 1000, 1, 1000, 1000)
    for index in range(111):
        start, x = volume_offset + index * 72, index * 3
        struct.pack_into("<4B6f", data, start, 6, 0, 0, 0, x, 0.5, 1, x + 1, 0.5, 2)
        struct.pack_into("<I", data, start + 60, 0x12345678)
    # One unrelated unsupported transition distinguishes it from preload portals.
    for index in range(13):
        start = portal_offset + index * 72
        struct.pack_into("<4B6f", data, start, int(index != 0), 0, 0, 0, 0, 0, 0, 1, 0, 1)
        struct.pack_into("<2I3f", data, start + 52, 0xFFFFFFFF, AK01, 0, 0, 0)
    for portal, zone, _, _, destination, camera, marker in SPECS[scene_id]:
        start, x = portal_offset + portal * 72, zone * 3
        struct.pack_into("<4B6f", data, start, 0, 0, camera, marker, x, 0.5, 1, x + 1, 0.5, 2)
        struct.pack_into("<2I3f", data, start + 52, 0xFFFFFFFF, destination, 0, math.pi, 0)
    if scene_id == AK01:
        start, x = volume_offset + 74 * 72, 74 * 3
        data[start + 1] = 2
        struct.pack_into("<12f", data, start + 4, x, 0.5, 1, x, 0.5, 2,
                         x + 1, 0.5, 2, x + 1, 0.5, 1)
    for index in range(15):
        start = camera_offset + index * 120
        struct.pack_into("<I", data, start + 84, 1)
        struct.pack_into("<7f", data, start + 92, 0, 0, 0, 0, 0, 0, 1)
    for index in range(53):
        struct.pack_into("<3f", data, marker_offset + index * 12, index + 0.125, 0.75, index * 0.5)
    return data


def source_package(scene_id, extra_guard=False, overwrite=False):
    """Use the existing synthetic JPC builder, with two source-shaped branches."""
    specs = SPECS[scene_id]
    activation = b"".join(bytes([0x10, portal, 0xB8, 0, 4]) for portal, *_ in specs) + b"\xb1"
    code = bytearray(b"\x03\x99\0\0" if extra_guard else b"")
    jumps = []
    for portal, zone, marker, angle, *_ in specs:
        code.extend(bytes([0x2B, 0x10, zone, 0xB6, 0, 2]))
        trigger_jump = len(code)
        code.extend(b"\x99\0\0")
        code.extend(bytes([0x2B, 0x10, marker, 0x10, angle, 0xB6, 0, 3]))
        facing_jump = len(code)
        code.extend(b"\x99\0\0\x10\x74\x3d")
        code.extend(bytes([0x10, portal, 0x12, 8, 0xB8, 0, 5]))
        end_jump = len(code)
        code.extend(b"\xa7\0\0")
        jumps.append((trigger_jump, facing_jump, end_jump, len(code)))
    end = len(code)
    if overwrite:
        code.extend(b"\x03\x3d")
    code.extend(b"\x1c\xac")
    if extra_guard:
        struct.pack_into(">h", code, 2, end - 1)
    for trigger, facing, skip, following in jumps:
        for offset, target in ((trigger, following), (facing, end), (skip, end)):
            struct.pack_into(">h", code, offset + 1, target - offset)
    name = b"SceneAK01" if scene_id == AK01 else b"SceneAK09"
    return ScriptPackage(fixture(activation=activation, allow=bytes(code)).replace(b"SceneTest\0", name + b"\0"))


class DoorTests(unittest.TestCase):
    def setUp(self):
        self.payloads = {scene_id: source_scene(scene_id) for scene_id in (AK01, AK09)}
        self.destinations = {scene_id: parse_portals(data, scene_id) for scene_id, data in self.payloads.items()}
        self.timing = {"delay_ticks": 30, "forced_enable_countdown": -15,
                       "latest_enable_ticks": 45, "input_guard_ticks": 45}
        timing = patch("ed_doors.extract_portal_timing", return_value=self.timing)
        timing.start()
        self.addCleanup(timing.stop)

    def decode(self, scene_id=AK01, payload=None, package=None, destinations=None):
        return decode_doors(self.payloads[scene_id] if payload is None else payload, scene_id,
                            source_package(scene_id) if package is None else package,
                            self.destinations if destinations is None else destinations)

    def test_source_order_and_unmodified_policy_geometry_and_timing(self):
        result = self.decode()
        self.assertEqual(result["scene_name"], "ak01")
        self.assertEqual(result["initially_disabled"], [2, 3])
        self.assertIs(result["timing"], self.timing)
        self.assertEqual([door["portal"]["index"] for door in result["doors"]], [3, 2])
        self.assertEqual([p["index"] for p in result["unsupported_portals"]], [0])
        door = result["doors"][1]
        self.assertEqual(door["interaction_zone"]["shape"], 2)
        self.assertEqual(door["interaction_zone"]["spatial_mask"], 0x12345678)
        self.assertEqual(door["interaction_zone"]["points"][0], [222, 0.5, 1])
        self.assertEqual(door["gate"]["marker_index"], 32)
        self.assertEqual(door["gate"]["action_assigned"], 116)
        self.assertIn("source_offsets", door["gate"])
        self.assertEqual(door["facing_markerXYZ"], [32.125, 0.75, 16])
        self.assertEqual(door["transition"]["marker_xyz"], [2.125, 0.75, 1])
        self.assertEqual(door["transition"]["spawn_xyz"], [2.125, 0, 1])

    def test_both_verified_reverse_pairs(self):
        forward, reverse = self.decode(), self.decode(AK09)
        self.assertEqual([(d["portal"]["index"], d["transition"]["scene_name"],
                           d["transition"]["camera_index"], d["transition"]["marker_index"])
                          for d in forward["doors"]], [(3, "ak09", 12, 3), (2, "ak09", 8, 2)])
        self.assertEqual([(d["portal"]["index"], d["interaction_zone"]["index"],
                           d["transition"]["camera_index"], d["transition"]["marker_index"])
                          for d in reverse["doors"]], [(8, 57, 11, 1), (7, 58, 3, 0)])

    def test_missing_destination_and_destination_bounds(self):
        with self.assertRaisesRegex(ValueError, "destination scene is missing"):
            self.decode(destinations={})
        for key, value in (("camera_count", 2), ("markers", []), ("scene_id", AK01)):
            destinations = copy.deepcopy(self.destinations)
            destinations[AK09][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.decode(destinations=destinations)

    def test_rejects_cross_pair_changes_and_preload_mode(self):
        offset = struct.unpack_from("<I", self.payloads[AK01], 0x2C)[0] + 3 * 72
        for position, value in ((0, 1), (2, 8), (3, 2), (56, AK01)):
            data = bytearray(self.payloads[AK01])
            struct.pack_into("<I" if position == 56 else "<B", data, offset + position, value)
            with self.subTest(position=position), self.assertRaisesRegex(ValueError, "cross-pair"):
                self.decode(payload=data)

    def test_rejects_bad_interaction_geometry_and_facing_marker(self):
        offset = struct.unpack_from("<I", self.payloads[AK01], 12)[0] + 69 * 72
        for position, fmt, value in ((offset, "<B", 0), (offset + 1, "<B", 1),
                                     (offset + 4, "<f", math.nan), (0x48, "<I", 10), (8, "<I", 50)):
            data = bytearray(self.payloads[AK01])
            struct.pack_into(fmt, data, position, value)
            with self.subTest(position=position), self.assertRaises(ValueError):
                self.decode(payload=data)

    def test_rejects_overlapping_other_interaction_and_additional_script_guards(self):
        data = bytearray(self.payloads[AK09])
        offset = struct.unpack_from("<I", data, 12)[0]
        data[offset + 58 * 72 + 4:offset + 58 * 72 + 28] = data[offset + 57 * 72 + 4:offset + 57 * 72 + 28]
        with self.assertRaisesRegex(ValueError, "overlaps"):
            self.decode(AK09, payload=data)
        with self.assertRaisesRegex(ValueError, "Additional condition"):
            self.decode(package=source_package(AK01, extra_guard=True))
        with self.assertRaisesRegex(ValueError, "overwritten"):
            self.decode(package=source_package(AK01, overwrite=True))

    def test_unknown_scene_is_explicitly_unsupported(self):
        with self.assertRaisesRegex(ValueError, "only supports"):
            decode_doors(b"", 0x61620100, source_package(AK01), {})


if __name__ == "__main__":
    unittest.main()
