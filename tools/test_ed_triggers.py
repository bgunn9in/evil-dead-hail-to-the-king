import math
import struct
import unittest

from ed_triggers import camera_rule_actions, contains, expected_camera, parse_triggers, update_membership


def make_scene():
    volume_offset = 88 + 64
    rule_offset = volume_offset + 2 * 72
    action_offset = rule_offset + 2 * 16
    condition_offset = action_offset + 2 * 8
    data = bytearray(condition_offset + 2 * 12)
    struct.pack_into("<4I", data, 0, 64, 88, 2, volume_offset)
    struct.pack_into("<II", data, 0x10, 2, rule_offset)
    struct.pack_into("<II", data, 0x18, 2, action_offset)
    struct.pack_into("<II", data, 0x20, 2, condition_offset)
    struct.pack_into("<I", data, 0x40, 3)
    struct.pack_into("<9f", data, 88, -10, -1, -10, 10, 1, 10, 1, 20, 20)
    points = [[[-2, 0, -2], [1, 0, 1]], [[0, 0, -1], [0, 0, 2], [3, 0, 2], [3, 0, -1]]]
    for index, polygon in enumerate(points):
        offset = volume_offset + index * 72
        data[offset + 1] = 0 if index == 0 else 2
        for pi, point in enumerate(polygon):
            struct.pack_into("<3f", data, offset + 4 + pi * 12, *point)
        struct.pack_into("<3I", data, offset + 52, 1, index, 0xFFFFFFFF)
        struct.pack_into("<4I", data, rule_offset + index * 16, 1, index, 1, index)
        struct.pack_into("<2I", data, action_offset + index * 8, 0, index + 1)
        struct.pack_into("<4BII", data, condition_offset + index * 12, 7, 1, 1, 0, 0, 12)
    return data


def make_camera_event_scene():
    """AT01's two authored AND rules, without original game dependencies."""
    raw = make_scene()
    volumes, rules, actions, conditions = [struct.unpack_from("<I", raw, o)[0] for o in (12, 20, 28, 36)]
    struct.pack_into("<I", raw, 0x40, 7)
    struct.pack_into("<2I", raw, volumes + 52, 2, 0)
    struct.pack_into("<2I", raw, volumes + 72 + 52, 0, 2)
    struct.pack_into("<4I", raw, rules, 1, 0, 2, 0)
    struct.pack_into("<4I", raw, rules + 16, 1, 1, 2, 2)
    struct.pack_into("<I", raw, actions + 4, 0)
    struct.pack_into("<I", raw, actions + 12, 6)
    raw[conditions:] = b"".join(struct.pack("<4BII", *values) for values in (
        (7, 1, 1, 1, 0, 7), (7, 8, 7, 0, 0x100, 1),
        (7, 1, 1, 1, 0, 7), (7, 8, 7, 0, 0x100, 0)))
    struct.pack_into("<I", raw, 0x20, 4)
    return raw


class TriggerTests(unittest.TestCase):
    def test_camera_event_requires_explicit_conditional_export_support(self):
        with self.assertRaisesRegex(ValueError, "combination"):
            parse_triggers(make_camera_event_scene())
        parsed = parse_triggers(make_camera_event_scene(), camera_events=True)
        self.assertTrue(parsed["requires_camera_events"])
        trigger, = parsed["triggers"]
        self.assertEqual(trigger["camera_actions"], [])
        self.assertEqual(len(trigger["conditional_rules"]), 2)
        self.assertEqual(camera_rule_actions(trigger, "enter", 1), [0])
        #Native408C25 rejects zero payload before equality, even for parameter0.
        for camera in (0, 2, 6, None):
            self.assertEqual(camera_rule_actions(trigger, "enter", camera), [])
        self.assertEqual(camera_rule_actions(trigger, "exit", 1), [])

    def test_camera_event_results_reset_each_frame_and_do_not_latch(self):
        parsed = parse_triggers(make_camera_event_scene(), camera_events=True)
        entered = update_membership(parsed, [-1, 0, -1], current_camera=1)
        self.assertEqual(entered["camera_actions"], [0])
        held = update_membership(parsed, [-1, 0, -1], entered["inside"], current_camera=1)
        self.assertEqual(held["camera_actions"], [])
        left = update_membership(parsed, [-5, 0, -5], held["inside"], 0)
        reentered = update_membership(parsed, [-1, 0, -1], left["inside"], 0)
        self.assertEqual(reentered["camera_actions"], [])

    def test_camera_event_uses_one_payload_for_all_rules_in_a_zone(self):
        parsed = parse_triggers(make_camera_event_scene(), camera_events=True)
        trigger, = parsed["triggers"]
        trigger["rules"][1]["camera_event"]["value"] = 1
        self.assertEqual(camera_rule_actions(trigger, "enter", 1), [0, 6])
        result = update_membership(parsed, [-1, 0, -1], current_camera=1)
        self.assertEqual(result["camera"], 6)

    def test_rule_actions_and_load_activation(self):
        data = parse_triggers(make_scene())
        self.assertEqual([t["camera_actions"] for t in data["triggers"]], [[1], [2]])
        self.assertEqual(data["zones"][0]["raw_enabled_byte"], 0)
        self.assertTrue(data["zones"][0]["enabled"])
        self.assertEqual(data["triggers"][0]["on_enter_actions"][0]["camera_index"], 1)

    def test_original_rectangle_and_quad_boundaries_ignore_y(self):
        data = parse_triggers(make_scene())
        rectangle, quad = data["zones"]
        self.assertTrue(contains(rectangle, [1, 100, 1]))
        self.assertFalse(contains(rectangle, [1.001, 0, 1]))
        self.assertFalse(contains(quad, [0, 0, 0]))
        self.assertTrue(contains(quad, [0.001, -100, 0]))
        self.assertFalse(contains(quad, [1, 0, 2]))

    def test_enter_only_source_order_and_reentry(self):
        data = parse_triggers(make_scene())
        state = update_membership(data, [0.5, 0, 0.5], current_camera=0)
        self.assertEqual(state["entered"], [0, 1])
        self.assertEqual(state["camera_actions"], [1, 2])
        self.assertEqual(state["camera"], 2)
        held = update_membership(data, [0.5, 0, 0.5], state["inside"], current_camera=0)
        self.assertEqual(held["entered"], [])
        self.assertEqual(held["camera"], 0)
        left = update_membership(data, [-5, 0, -5], held["inside"], held["camera"])
        self.assertEqual(left["inside"], [])
        self.assertEqual(expected_camera(data, [-1, 0, -1], 0, left["inside"]), 1)

    def test_native_triangle_halfplanes_are_strict_and_ignore_query_height(self):
        triangle = {"shape": 1, "points": [[0, 7, 0], [0, 8, 2], [2, 9, 0]]}
        self.assertTrue(contains(triangle, [0.5, -100, 0.5]))
        self.assertFalse(contains(triangle, [0, 7, 1]))
        self.assertFalse(contains(triangle, [1, 7, 1]))
        self.assertFalse(contains(triangle, [1.1, 7, 1.1]))

    def test_parser_retains_source_triangle_camera_zone(self):
        raw = make_scene()
        offset = struct.unpack_from("<I", raw, 12)[0] + 72
        raw[offset + 1] = 1
        for index, point in enumerate(((0, 7, -1), (0, 8, 2), (3, 9, -1))):
            struct.pack_into("<3f", raw, offset + 4 + index * 12, *point)
        parsed = parse_triggers(raw)
        triangle = parsed["zones"][1]
        self.assertEqual(triangle["shape"], 1)
        self.assertEqual(len(triangle["points"]), 3)
        self.assertTrue(contains(triangle, [.5, -100, 0]))
        self.assertFalse(contains(triangle, [1, 100, 1]))
        self.assertEqual(expected_camera(parsed, [.5, 0, 0], 0), 2)

    def test_native_sphere_shape3_4_and_box_shape5(self):
        for shape in (3, 4):
            sphere = {"shape": shape, "points": [[1, 2, 3], [0, 2, 0]]}
            self.assertTrue(contains(sphere, [1, 4, 3]))
            self.assertFalse(contains(sphere, [1, 4.001, 3]))
            sphere["points"][1] = [0, 0, 0]
            self.assertTrue(contains(sphere, [1, 2, 3]))
            self.assertFalse(contains(sphere, [1.0001, 2, 3]))
        box = {"shape": 5, "points": [[0, 1, 2], [3, 4, 5]]}
        self.assertTrue(contains(box, [0, 1, 2]))
        self.assertTrue(contains(box, [3, 4, 5]))
        self.assertFalse(contains(box, [1, .9999, 3]))
        #419A35 stores only query distance squared asfloat; radius stayswide.
        values = [struct.unpack("<f", struct.pack("<f", v))[0] for v in (.2, .3, .4)]
        self.assertFalse(contains({"shape": 3, "points": [[0, 0, 0], values]}, values))

    def test_skipped_broadphase_preserves_original_membership(self):
        data = parse_triggers(make_scene())
        data["zones"][0]["spatial_mask"] = 0
        result = update_membership(data, [-5, 0, -5], [0], 2)
        self.assertEqual(result["inside"], [0])
        self.assertEqual(result["camera"], 2)

    def test_exit_rule_and_chain_zero_preserve_first_condition(self):
        raw = make_scene()
        condition_offset = struct.unpack_from("<I", raw, 0x24)[0]
        struct.pack_into("<B", raw, condition_offset + 1, 2)
        raw.extend(struct.pack("<4BII", 0, 0, 0, 0, 0, 2))
        struct.pack_into("<I", raw, 0x20, 3)
        rule_offset = struct.unpack_from("<I", raw, 0x14)[0]
        # First rule references the existing condition plus an ignored chain=0 record.
        first = raw[condition_offset:condition_offset + 12]
        second = raw[condition_offset + 12:condition_offset + 24]
        extra = raw[condition_offset + 24:condition_offset + 36]
        raw[condition_offset:] = first + extra + second
        struct.pack_into("<4I", raw, rule_offset, 1, 0, 2, 0)
        struct.pack_into("<4I", raw, rule_offset + 16, 1, 1, 1, 2)
        data = parse_triggers(raw)
        self.assertEqual(data["triggers"][0]["camera_actions"], [])
        self.assertEqual(data["triggers"][0]["exit_camera_actions"], [1])
        result = update_membership(data, [-5, 0, -5], [0], 2)
        self.assertEqual(result["exited"], [0])
        self.assertEqual(result["camera"], 1)

    def test_dangling_last_condition_operator_is_not_an_extra_predicate(self):
        raw = make_scene()
        conditions = struct.unpack_from("<I", raw, 0x24)[0]
        raw[conditions + 3] = 3
        parsed = parse_triggers(raw)
        self.assertEqual(parsed["triggers"][0]["rules"][0]["conditions"][0]["chain"], 3)
        self.assertEqual(expected_camera(parsed, [-1, 0, -1], 0), 1)

    def test_rejects_invalid_references_coordinates_and_conditions(self):
        base = make_scene()
        action_offset = struct.unpack_from("<I", base, 0x1C)[0]
        condition_offset = struct.unpack_from("<I", base, 0x24)[0]
        volume_offset = struct.unpack_from("<I", base, 0x0C)[0]
        mutations = [(lambda d: d[:80]),
                     (lambda d: struct.pack_into("<I", d, 0x1C, len(d)) or d),
                     (lambda d: struct.pack_into("<I", d, action_offset + 4, 99) or d),
                     (lambda d: struct.pack_into("<f", d, volume_offset + 4, math.nan) or d),
                     (lambda d: struct.pack_into("<B", d, condition_offset + 1, 0) or d)]
        for change in mutations:
            with self.subTest(change=change), self.assertRaises(ValueError):
                parse_triggers(change(make_scene()))


if __name__ == "__main__":
    unittest.main()
