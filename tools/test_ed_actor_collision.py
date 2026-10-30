import unittest
from dataclasses import replace
from ed_actor_collision import Collider, first_actor_contact, horizontal_query_required, radius_cm, static_class_accept, zero_horizontal_step


class ActorCollisionTests(unittest.TestCase):
    def setUp(self):
        self.npc = Collider(1, 4, 0, (1, .5, 0), 30)
        self.ash = Collider(0, 0, 0, (0, 100, 0), 80)

    def test_zero_and_vertical_are_not_overlap_recovery(self):
        for dy in (0, .25):
            got = zero_horizontal_step((.4, .5, 0), (0, dy, 0), 3.25)
            self.assertEqual(got, {"local": (.4000000059604645, .5 + dy, 0), "world_y": 3.75 + dy, "contact_bits": 0})
        self.assertFalse(horizontal_query_required((-0., 1, -0.)))
        self.assertTrue(horizontal_query_required((1e-8, 0, 0)))
        with self.assertRaises(ValueError): zero_horizontal_step((0, 0, 0), (1, 0, 0), 0)

    def test_source_centimetre_float_roundtrip(self):
        self.assertEqual(radius_cm(80), .7999999523162842)
        self.assertEqual(radius_cm(30), .29999998211860657)

    def test_current_overlap_approach_ignores_height(self):
        self.assertEqual(first_actor_contact(self.npc, (-.05, 7, 0), [self.ash]), {"slot": 0, "type": 0, "index": 0, "contact_bits": 4})

    def test_outside_entering_is_not_a_sweep(self):
        outside = replace(self.npc, local=(1.2, 0, 0))
        self.assertIsNone(first_actor_contact(outside, (-.5, 0, 0), [self.ash]))

    def test_overlap_may_move_away(self):
        self.assertIsNone(first_actor_contact(self.npc, (.05, 0, 0), [self.ash]))
        self.assertIsNone(first_actor_contact(self.npc, (0, 0, .1), [self.ash]))
        self.assertIsNone(first_actor_contact(self.npc, (0, .1, 0), [self.ash]))

    def test_target_and_mover_gates(self):
        for changes in ({"flags": 0}, {"flags": 9}, {"slot": -1}, {"radius_cm": 0}, {"query_mask": 2}, {"aux_state": 1}):
            self.assertIsNone(first_actor_contact(self.npc, (-.05, 0, 0), [replace(self.ash, **changes)]))
        self.assertIsNone(first_actor_contact(replace(self.npc, query_mask=2), (-.05, 0, 0), [self.ash]))
        self.assertIsNone(first_actor_contact(replace(self.npc, radius_cm=0), (-.05, 0, 0), [self.ash]))
        self.assertIsNotNone(first_actor_contact(self.npc, (-.05, 0, 0), [replace(self.ash, flags=0x41)]))

    def test_first_physical_slot_not_nearest_or_key_order(self):
        moving = replace(self.npc, local=(0, 0, 0))
        near = Collider(8, 2, 0, (.5, 0, 0), 80)
        far = Collider(2, 35, 0, (.8, 0, 0), 80)
        self.assertEqual(first_actor_contact(moving, (.1, 0, 0), [near, far])["slot"], 2)

    def test_static_class_enabled_and_shape_flags(self):
        for cls, flag in ((1, 8), (2, 4), (4, 2)):
            self.assertTrue(static_class_accept(flag, cls))
            self.assertFalse(static_class_accept(0, cls))
            self.assertFalse(static_class_accept(flag, cls, enabled=0))
            self.assertTrue(static_class_accept(0xe, cls))
        self.assertFalse(static_class_accept(0xffffffff, 2, shape=5))
        self.assertTrue(static_class_accept(0, 3))  # Native default class has no bitfilter.

    def test_bounds(self):
        with self.assertRaises(ValueError): first_actor_contact(self.npc, (-.1, 0, 0), [replace(self.ash, slot=13)])
        with self.assertRaises(ValueError): first_actor_contact(self.npc, (-.1, 0, 0), [self.ash, self.ash])
        with self.assertRaises(ValueError): zero_horizontal_step((0, float("nan"), 0), (0, 0, 0), 0)


if __name__ == "__main__": unittest.main()
