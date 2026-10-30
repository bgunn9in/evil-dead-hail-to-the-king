import struct
import unittest

from ed_collision import parse_collision, point_spatial_mask


def make_volume(kind, shape, points):
    data = bytearray(b"\xCD" * 72)
    struct.pack_into("<4B", data, 0, kind, shape, 0, 0)
    for index, point in enumerate(points):
        struct.pack_into("<3f", data, 4 + index * 12, *point)
    struct.pack_into("<5I", data, 52, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF)
    return data


def make_scene():
    floor = bytearray(64)
    struct.pack_into("<9f3I", floor, 0, 0, -1, 0, 10, 1, 10, 1, 10, 10, 3, 1, 1)
    for point in ((0, 0.5, 0), (10, 0.5, 0), (0, 0.5, 10)):
        floor.extend(struct.pack("<3f", *point))
    floor.extend(struct.pack("<3f4HI", 0, 1, 0, 0, 1, 2, 0xCDCD, 0xFFFF7FFE))
    floor.extend(b"\0" * 4)
    data = bytearray(88)
    struct.pack_into("<4I", data, 0, len(floor), 88, 2, 88 + len(floor))
    data.extend(floor)
    data.extend(make_volume(2, 0, [(4, 0, 3), (2, 0, 1)]))
    data.extend(make_volume(1, 2, [(0, 0, 0), (0, 0, 10), (10, 0, 10), (10, 0, 0)]))
    return data


class CollisionTests(unittest.TestCase):
    def test_floor_and_uninflated_polygon_geometry(self):
        decoded = parse_collision(make_scene())
        self.assertEqual(decoded["floor"]["triangles"], [[0, 1, 2]])
        self.assertEqual(decoded["floor"]["vertices"][0], [0, 0.5, 0])
        self.assertEqual(decoded["floor"]["surface_flags_raw"], [0xFFFF7FFE])
        self.assertEqual(decoded["floor"]["undecoded_tail_bytes"], 4)
        self.assertEqual(decoded["solids"][0]["vertices_xz"], [[2, 1], [2, 3], [4, 3], [4, 1]])
        self.assertEqual(len(decoded["regions"]), 1)
        self.assertEqual(decoded["regions"][0]["type"], 1)

    def test_spatial_mask_and_source_shift_wrapping(self):
        decoded = parse_collision(make_scene())
        self.assertEqual(point_spatial_mask(decoded, 0, 0), 0x00010001)
        self.assertEqual(point_spatial_mask(decoded, 5, 5), 0x01000100)
        self.assertEqual(point_spatial_mask(decoded, 10, 10), 0x00010001)
        with self.assertRaises(ValueError):
            point_spatial_mask(decoded, float("nan"), 0)

    def test_original_triangle_shape_preserves_three_vertices(self):
        data = make_scene()
        offset = struct.unpack_from("<I", data, 12)[0]
        points = [(2, 0, 1), (2, 0, 3), (4, 0, 1)]
        data[offset:offset + 72] = make_volume(2, 1, points)
        result = parse_collision(data)["solids"][0]
        self.assertEqual(result["vertices_xz"], [[2, 1], [2, 3], [4, 1]])
        self.assertEqual(result["source_vertices_xyz"], [list(p) for p in points])

    def test_native_sphere_movement_uses_xz_center_and_radius_vector_length(self):
        data = make_scene()
        offset = struct.unpack_from("<I", data, 12)[0]
        data[offset:offset + 72] = make_volume(2, 3, [(2, 50, 3), (0, 0, 0.45)])
        result = parse_collision(data)["solids"][0]
        self.assertEqual(result["circle_xz"]["center"], [2, 3])
        self.assertAlmostEqual(result["circle_xz"]["radius"], 0.45)
        self.assertEqual(result["vertices_xz"], [])

    def test_shape4_5_point_volumes_do_not_create_source_segment_walls(self):
        for shape, points in ((4, [(2, 50, 3), (0, 0, .45)]), (5, [(0, 0, 0), (4, 10, 8)])):
            data = make_scene()
            offset = struct.unpack_from("<I", data, 12)[0]
            data[offset:offset + 72] = make_volume(2, shape, points)
            result = parse_collision(data)
            self.assertEqual(result["solids"], [])
            point, = result["point_only_solids"]
            self.assertEqual(point["volume_index"], 0)
            self.assertEqual(point["shape"], shape)
            self.assertFalse(point["source_segment_collision"])

    def test_invalid_header_array_bounds_and_indices(self):
        for offset, value in ((0, 0xFFFFFFFF), (4, 0), (8, 0xFFFFFFFF), (12, 0), (88 + 36, 0xFFFFFFFF)):
            with self.subTest(offset=offset):
                data = make_scene()
                struct.pack_into("<I", data, offset, value)
                with self.assertRaises(ValueError):
                    parse_collision(data)
        data = make_scene()
        struct.pack_into("<H", data, 88 + 64 + 36 + 12, 3)
        with self.assertRaises(ValueError):
            parse_collision(data)
        with self.assertRaises(ValueError):
            parse_collision(make_scene()[:-1])

    def test_invalid_static_shape_and_degenerate_coordinates(self):
        data = make_scene()
        volume_offset = struct.unpack_from("<I", data, 12)[0]
        data[volume_offset + 1] = 6
        with self.assertRaises(ValueError):
            parse_collision(data)
        data = make_scene()
        struct.pack_into("<f", data, volume_offset + 4, float("nan"))
        with self.assertRaises(ValueError):
            parse_collision(data)
        data = make_scene()
        struct.pack_into("<f", data, volume_offset + 4, 2)
        with self.assertRaises(ValueError):
            parse_collision(data)
        data = make_scene()
        struct.pack_into("<3f", data, volume_offset + 72 + 4 + 2 * 12, 1, 0, 1)
        with self.assertRaises(ValueError):
            parse_collision(data)

    def test_navigation_source_points_links_and_checked_indices(self):
        data = make_scene()
        point_offset = len(data)
        struct.pack_into("<2I", data, 0x30, 2, point_offset)
        for point in ((1, 0.5, 1), (5, 0.5, 5)):
            record = bytearray(44)
            struct.pack_into("<3f", record, 8, *point)
            data.extend(record)
        link_offset = len(data)
        struct.pack_into("<2I", data, 0x38, 2, link_offset)
        for pair in ((0, 1), (1, 0)):
            record = bytearray(32)
            struct.pack_into("<2I", record, 24, *pair)
            data.extend(record)
        decoded = parse_collision(data)
        self.assertEqual(decoded["navigation"], {"points": [[1, 0.5, 1], [5, 0.5, 5]], "links": [[0, 1], [1, 0]]})
        struct.pack_into("<I", data, link_offset + 24, 2)
        with self.assertRaises(ValueError):
            parse_collision(data)


if __name__ == "__main__":
    unittest.main()
