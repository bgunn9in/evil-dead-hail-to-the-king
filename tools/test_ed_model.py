from io import BytesIO
import struct
import unittest

from PIL import Image

from ed_model import decode_animations, decode_mesh, decode_skeleton, decode_texture, pose_mesh, sample_pose


def make_mesh():
    data = bytearray(struct.pack("<6I", 1, 8, 0, 3, 3, 1))
    for x, y in ((0, 0), (1, 0), (0, 1)):
        data.extend(struct.pack("<6fI2f", x, y, 0, 0, 0, 1, 0xFFFFFFFF, x, y))
    data.extend(struct.pack("<6H", 0, 1, 1, 0, 1, 2))
    return data


def make_skeleton():
    data = bytearray(struct.pack("<3I", 0, 2, 0))
    identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    data.extend(struct.pack("<19fi", *identity, 10, 0, 0, -1))
    rotation = [0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    data.extend(struct.pack("<19fi", *rotation, 0, 2, 0, 0))
    return data


def make_animation():
    header = struct.pack("<5I4H3I", 0x01024084, 0, 0, 0, 0, 2, 2, 1, 1, 0, 0, 0)
    curves = [(3, 4), (5, 6), (7, 8)] + [(0, 0)] * 6
    payload = header + struct.pack("<18f", *(value for curve in curves for value in curve))
    payload += b"e" * 36 + b"f" * 20
    return struct.pack("<2I", 1, 8) + payload


class ModelTests(unittest.TestCase):
    def test_geometry_bone_assignments_and_row_vector_hierarchy(self):
        mesh = decode_mesh(make_mesh())
        self.assertEqual(mesh[0]["triangles"], [[0, 1, 2]])
        self.assertEqual(mesh[0]["bone_indices"], [0, 1, 1])
        posed = pose_mesh(mesh, decode_skeleton(make_skeleton()))
        self.assertEqual(posed[0]["positions"], [[10, 0, 0], [10, 3, 0], [9, 2, 0]])
        self.assertEqual(posed[0]["normals"], [[0, 0, 1]] * 3)

    def test_malformed_geometry(self):
        for offset, value in ((0, 0xFFFFFFFF), (4, 0), (12, 0xFFFFFFFF), (16, 4), (20, 2)):
            with self.subTest(offset=offset):
                data = make_mesh()
                struct.pack_into("<I", data, offset, value)
                with self.assertRaises(ValueError):
                    decode_mesh(data)
        data = make_mesh()
        struct.pack_into("<H", data, len(data) - 2, 3)
        with self.assertRaises(ValueError):
            decode_mesh(data)
        with self.assertRaises(ValueError):
            decode_mesh(make_mesh()[:-4])

    def test_invalid_skeleton_and_bone_reference(self):
        data = make_skeleton()
        struct.pack_into("<i", data, 12 + 76, 1)
        with self.assertRaises(ValueError):
            decode_skeleton(data)
        with self.assertRaises(ValueError):
            decode_skeleton(make_skeleton()[:-1])
        mesh = decode_mesh(make_mesh())
        mesh[0]["bone_indices"][0] = 2
        with self.assertRaises(ValueError):
            pose_mesh(mesh, decode_skeleton(make_skeleton()))

    def test_embedded_dds_and_checked_length(self):
        stream = BytesIO()
        Image.new("RGBA", (4, 4), (20, 40, 60, 255)).save(stream, format="DDS")
        payload = stream.getvalue()
        data = struct.pack("<I", 0x08024000) + b"ash.dds\0".ljust(128, b"\0") + struct.pack("<I", len(payload)) + payload
        image, name = decode_texture(data)
        self.assertEqual(name, "ash.dds")
        self.assertEqual(image.getpixel((0, 0)), (20, 40, 60, 255))
        with self.assertRaises(ValueError):
            decode_texture(data[:-1])

    def test_animation_curves_events_and_root_y_replacement(self):
        clip = decode_animations(make_animation())[0]
        self.assertEqual(clip["root_positions"], [[3, 5, 7], [4, 6, 8]])
        self.assertEqual(clip["rotations_degrees"], [[[0, 0, 0]] * 2] * 2)
        self.assertEqual(clip["events36_raw"], [(b"e" * 36).hex()])
        self.assertEqual(clip["events20_raw"], [(b"f" * 20).hex()])
        posed = sample_pose(decode_mesh(make_mesh()), decode_skeleton(make_skeleton()), clip, 1)
        self.assertEqual(posed[0]["positions"], [[10, 6, 0], [10, 9, 0], [9, 8, 0]])
        with self.assertRaises(ValueError):
            sample_pose(decode_mesh(make_mesh()), decode_skeleton(make_skeleton()), clip, 2)

    def test_invalid_animation_ranges_and_counts(self):
        for offset, value in ((0, 0xFFFFFFFF), (4, 0), (28, 0), (32, 0xFFFFFFFF)):
            with self.subTest(offset=offset):
                data = bytearray(make_animation())
                struct.pack_into("<I", data, offset, value)
                with self.assertRaises(ValueError):
                    decode_animations(data)
        with self.assertRaises(ValueError):
            decode_animations(make_animation()[:-4])

    def test_animation_xyz_order_precedes_bind_rotation(self):
        bone = decode_skeleton(make_skeleton())[0]
        bone["translation"] = [0, 0, 0]
        bone["matrix"] = [[0, 1, 0, 0], [-1, 0, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
        clip = {"bone_count": 1, "frame_count": 1, "root_positions": [[0, 0, 0]],
                "rotations_degrees": [[[90, 90, 0]]]}
        part = {"positions": [[0, 1, 0]], "normals": [[0, 1, 0]], "bone_indices": [0]}
        posed = sample_pose([part], [bone], clip, 0)[0]
        for actual, expected in zip(posed["positions"][0], (0, 1, 0)):
            self.assertAlmostEqual(actual, expected)


if __name__ == "__main__":
    unittest.main()
