import math
import struct
import unittest

from ed_camera import parse_scene, view_projection


def transform(matrix, point):
    return [sum(matrix[col * 4 + row] * point[col] for col in range(4))
            for row in range(4)]


def make_scene():
    data = bytearray(88 + 92 + 2 * 28 + 24)
    struct.pack_into("<II", data, 0x40, 1, 88)
    struct.pack_into("<II", data, 0x48, 2, len(data) - 24)
    data[88] = 4
    struct.pack_into("<I", data, 88 + 0x54, 2)
    for index in range(2):
        struct.pack_into("<7f", data, 88 + 92 + index * 28,
                         1, 2, 3 + index, 0, 0, 0, math.pi / 2)
    struct.pack_into("<6f", data, len(data) - 24, 10, 0, 20, 11, 0, 21)
    return data


class CameraTests(unittest.TestCase):
    def test_track_frames_and_original_markers(self):
        scene = parse_scene(make_scene())
        self.assertEqual(scene["markers"], [[10, 0, 20], [11, 0, 21]])
        self.assertEqual(scene["navpoints"], [])
        track = scene["tracks"][0]
        self.assertEqual(track["background_start"], 4)
        self.assertEqual([f["background_index"] for f in track["frames"]], [4, 5])
        self.assertEqual(track["frames"][1]["eye"], [1, 2, 4])

    def test_rejects_truncated_sections_and_bad_camera(self):
        for change in (lambda d: d[:80], lambda d: d[:-1],
                       lambda d: struct.pack_into("<I", d, 88 + 0x54, 1000) or d,
                       lambda d: struct.pack_into("<f", d, 88 + 92, math.nan) or d,
                       lambda d: struct.pack_into("<f", d, 88 + 92 + 24, 0) or d):
            with self.subTest(change=change), self.assertRaises(ValueError):
                parse_scene(change(make_scene()))

    def test_near_far_and_camera_translation(self):
        frame = {"eye": [1, 2, 3], "rotation": [0, 0, 0], "fov": math.pi / 2}
        camera = view_projection(frame)
        self.assertEqual(transform(camera["view16"], [1, 2, 3, 1]), [0, 0, 0, 1])
        for distance, depth in [(0.05, 0), (625, 1)]:
            clip = transform(camera["viewProjection16"], [1, 2, 3 + distance, 1])
            self.assertAlmostEqual(clip[0], 0)
            self.assertAlmostEqual(clip[1], 0)
            self.assertAlmostEqual(clip[2] / clip[3], depth)
        clip = transform(camera["viewProjection16"], [1 + 4 / 3, 3, 4, 1])
        self.assertAlmostEqual(clip[0] / clip[3], 1)
        self.assertAlmostEqual(clip[1] / clip[3], 1)

    def test_yaw_sign_and_roll(self):
        frame = {"eye": [0, 0, 0], "rotation": [0, math.pi / 2, 0], "fov": 1}
        camera = view_projection(frame)
        for actual, expected in zip(camera["forward3"], [1, 0, 0]):
            self.assertAlmostEqual(actual, expected)
        clip = transform(camera["viewProjection16"], [2, 0, 0, 1])
        self.assertAlmostEqual(clip[0], 0)
        self.assertGreater(clip[3], 0)
        frame["rotation"] = [0, 0, math.pi / 2]
        for actual, expected in zip(view_projection(frame)["right3"], [0, 1, 0]):
            self.assertAlmostEqual(actual, expected)

    def test_projection_validation(self):
        frame = {"eye": [0, 0, 0], "rotation": [0, 0, 0], "fov": 1}
        for options in ({"aspect": 0}, {"near": 0}, {"far": 0.01}, {"far": math.inf}):
            with self.subTest(options=options), self.assertRaises(ValueError):
                view_projection(frame, **options)

    def test_combined_rotation_order(self):
        frame = {"eye": [0, 0, 0], "rotation": [math.pi / 2] * 3, "fov": 1}
        camera = view_projection(frame)
        for actual, expected in zip(camera["right3"], [0, 0, -1]):
            self.assertAlmostEqual(actual, expected)
        for actual, expected in zip(camera["forward3"], [1, 0, 0]):
            self.assertAlmostEqual(actual, expected)


if __name__ == "__main__":
    unittest.main()
