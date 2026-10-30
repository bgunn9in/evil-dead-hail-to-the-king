from io import BytesIO
import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
import wave

from ed_assets import DATA_OFFSET, parse_archive
from ed_triggers import parse_triggers, update_membership
from export_scene import (embedded_wave, export_camera_frames, export_scene, export_scripts, floor_boundary_edges, floor_outline,
                          ground_axis, pack_camera_frames, pack_collision, pack_floor, pack_pose, pack_triggers, source_floor_height)
from ed_camera import parse_scene
from test_ed_scripts import fixture
from test_ed_triggers import make_camera_event_scene, make_scene


class SceneExportTests(unittest.TestCase):
    def test_all_camera_frame_binary_counts_basis_and_eye(self):
        from test_ed_camera import make_scene as camera_scene
        tracks = parse_scene(camera_scene())["tracks"]
        encoded = pack_camera_frames(tracks)
        self.assertEqual(encoded[:8], b"EDCAMF1\0")
        self.assertEqual(struct.unpack_from("<2I", encoded, 8), (1, 2))
        self.assertEqual(len(encoded), 16 + 2 * 29 * 4)
        frame0 = struct.unpack_from("<29f", encoded, 16)
        frame1 = struct.unpack_from("<29f", encoded, 16 + 116)
        self.assertEqual(frame0[16:], (1, 0, 0, 1, 1, 2, 3, 1, 0, 0, 0, 1, 0))
        self.assertEqual(frame1[20:23], (1, 2, 4))
        self.assertNotEqual(frame0[:16], frame1[:16])
        tracks[0]["frames"][1]["index"] = 0
        with self.assertRaisesRegex(ValueError, "frame order"):
            pack_camera_frames(tracks)

    def test_trigger3_preserves_conditions_and_legacy_remains_version2(self):
        self.assertEqual(pack_triggers(parse_triggers(make_scene()))[:8], b"EDTRIG2\0")
        data = pack_triggers(parse_triggers(make_camera_event_scene(), camera_events=True))
        self.assertEqual(data[:8], b"EDTRIG3\0")
        self.assertEqual(struct.unpack_from("<4I", data, 28), (0, 2, 2, 0xffffffff))
        self.assertEqual(struct.unpack_from("<5I", data, 60), (1, 1, 1, 1, 0))
        self.assertEqual(struct.unpack_from("<5I", data, 80), (1, 1, 0, 1, 6))

    @unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
    def test_original_at01_conditional_camera_rules(self):
        path = Path(__file__).resolve().parents[1] / "game/data/at01.pc"
        if not path.is_file():
            self.skipTest("Original user game not present")
        archive = path.read_bytes()
        self.assertEqual(hashlib.sha256(archive).hexdigest(), "8e636540190316fc39b1a5e28598bf6e0aed89415e6b6826de1b2c283da093e4")
        entry, = [entry for entry in parse_archive(archive) if entry.resource_id >> 24 == 15]
        parsed = parse_triggers(archive[entry.offset:entry.offset + entry.size], camera_events=True)
        self.assertEqual(len(parsed["triggers"]), 7)
        trigger = parsed["triggers"][-1]
        self.assertEqual(trigger["zone_index"], 16)
        self.assertEqual([(r["index"], r["camera_event"]["value"], r["actions"][0]["camera_index"])
                          for r in trigger["rules"]], [(6, 1, 0), (7, 0, 6)])
        point = [.47781842947006226, 0, -3.5444799661636353]
        for camera, actions in ((0, []), (1, [0]), (6, [])):
            self.assertEqual(update_membership(parsed, point, current_camera=camera)["camera_actions"], actions)
        encoded = pack_triggers(parsed)
        self.assertEqual(encoded[:8], b"EDTRIG3\0")
        self.assertEqual(len(encoded), 508)

    def test_triangle_packing_preserves_in_place_root(self):
        part = {"positions": [[1, 2, 3], [4, 5, 6], [7, 8, 9]],
                "normals": [[0, 1, 0]] * 3, "uv": [[0, 0], [1, 0], [0, 1]],
                "triangles": [[2, 0, 1]]}
        parts = [{**part, "triangles": []} for _ in range(11)]
        parts[0] = part
        data = pack_pose(parts)
        self.assertEqual(len(data), 3 * 32)
        self.assertEqual(struct.unpack_from("<8f", data), (7, 8, 9, 0, 1, 0, 0, 1))

    def test_camera_ground_projection(self):
        self.assertEqual(ground_axis([3, 10, 4]), [0.6, 0, 0.8])
        with self.assertRaises(ValueError):
            ground_axis([0, 1, 0])

    def test_export_refuses_original_game_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory)
            for target in (game, game / "generated"):
                with self.assertRaises(ValueError):
                    export_scene(game, target)
                with self.assertRaises(ValueError):
                    export_camera_frames(game, target, "ak01", {})

    def test_script_export_validates_both_packages_before_writing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            game, output = root / "game", root / "generated"
            source = game / "data/script/pc"
            source.mkdir(parents=True)
            data = fixture()
            (source / "levelA.jpc").write_bytes(data)
            (source / "levelB.jpc").write_bytes(b"bad")
            with self.assertRaises(ValueError):
                export_scripts(game, output)
            self.assertFalse(output.exists())
            (source / "levelB.jpc").write_bytes(data)
            self.assertEqual(set(export_scripts(game, output)), {"levelA", "levelB"})
            self.assertEqual((output / "levelA.jpc").read_bytes(), data)
            self.assertEqual((source / "levelA.jpc").read_bytes(), data)
            with self.assertRaises(ValueError):
                export_scripts(game, game / "generated")

    def test_floor_contour_merges_duplicate_triangle_vertices(self):
        floor = {"vertices": [[0, 0.5, 0], [10, 0.5, 0], [0, 0.5, 10],
                              [10, 0.5, 10], [10, 0.5, 0], [0, 0.5, 10]],
                 "triangles": [[2, 1, 0], [5, 3, 4]]}
        contour, height = floor_outline(floor)
        self.assertEqual(height, 0.5)
        self.assertEqual({tuple(point) for point in contour}, {(0, 0), (10, 0), (10, 10), (0, 10)})
        floor["vertices"][0][1] = 1
        with self.assertRaises(ValueError):
            floor_outline(floor)

    def test_disconnected_floor_is_not_silently_filled(self):
        floor = {"vertices": [[0, 0, 0], [1, 0, 0], [0, 0, 1],
                              [5, 0, 5], [6, 0, 5], [5, 0, 6]],
                 "triangles": [[0, 1, 2], [3, 4, 5]]}
        with self.assertRaises(ValueError):
            floor_outline(floor)
        data, height = pack_collision({"floor": floor, "solids": []})
        self.assertEqual(height, 0)
        self.assertEqual(struct.unpack_from("<8sIf", data), (b"EDCOLL1\0", 6, .25))
        edges = set()
        for offset in range(16, len(data), 24):
            self.assertEqual(struct.unpack_from("<2I", data, offset), (2, 2))
            a, b, c, d = struct.unpack_from("<4f", data, offset + 8)
            edges.add(tuple(sorted(((a, b), (c, d)))))
        self.assertEqual(edges, {tuple(sorted((tuple(a[::2]), tuple(b[::2])))) for a, b in floor_boundary_edges(floor)})
        self.assertFalse(any(a[0] < 2 and b[0] > 4 for a, b in edges))
        invalid = {"vertices": [[0,0,0],[1,0,0],[0,0,1],[0,0,-1],[0,0,2]],
                   "triangles": [[0,1,2],[0,1,3],[0,1,4]]}
        with self.assertRaisesRegex(ValueError, "Non-manifold"):
            pack_collision({"floor": invalid, "solids": []})

    def test_floor_boundary_ignores_only_zero_area_and_exact_repeated_faces(self):
        floor = {"vertices": [[0,0,0],[2,0,0],[0,0,2]],
                 "triangles": [[0,1,2],[2,1,0],[0,0,1],[0,0,0]]}
        self.assertEqual(len(floor_boundary_edges(floor)), 3)
        self.assertEqual(struct.unpack_from("<I", pack_floor(floor), 8)[0], 4)

    @unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
    def test_original_b_floor_placeholders_preserve_raw_triangles(self):
        from export_episode import base, load_contexts
        if not (base.ROOT / "game/data/bm01.pc").is_file():
            self.skipTest("Original B scenes unavailable")
        contexts, _ = load_contexts(base.ROOT / "game", ("bm01","bm11","ba01","ba02","ba11"))
        for name, context in contexts.items():
            floor = context["collision"]["floor"]
            self.assertEqual(len(floor_boundary_edges(floor)), 80 if name.startswith("bm") else 68)
            self.assertEqual(struct.unpack_from("<I", pack_floor(floor), 8)[0], 124 if name.startswith("bm") else 118)

    def test_sloped_floor_keeps_heights_and_discards_internal_edges(self):
        floor = {"vertices": [[0, 0, 0], [2, 1, 0], [0, 0, 2], [2, 1, 2]],
                 "triangles": [[2, 1, 0], [2, 3, 1]]}
        self.assertEqual(len(floor_boundary_edges(floor)), 4)
        self.assertAlmostEqual(source_floor_height(floor, 0.5, 0.5), 0.25)
        self.assertAlmostEqual(source_floor_height(floor, 1, 1), 0.5)
        with self.assertRaises(ValueError):
            source_floor_height(floor, 5, 5)
        encoded = pack_floor(floor)
        self.assertEqual(struct.unpack_from("<9f", encoded, 12), (0, 0, 2, 2, 1, 0, 0, 0, 0))
        collision, height = pack_collision({"floor": floor, "solids": []})
        self.assertIsNone(height)
        self.assertEqual(struct.unpack_from("<2I", collision, 16), (2, 2))

    def test_sound_uses_checked_riff_length_not_truncated_bundle_field(self):
        sound = BytesIO()
        with wave.open(sound, "wb") as output:
            output.setnchannels(1)
            output.setsampwidth(2)
            output.setframerate(22050)
            output.writeframes(b"\0\0" * 40000)
        wav = sound.getvalue()
        bundle = struct.pack("<4I", 1, 0x0E8A4ACA, 16, len(wav) & 0xFFFF) + wav
        data = bytearray(b"\xff" * DATA_OFFSET)
        struct.pack_into("<6I", data, 0, 0, 0x30004, 0, 20, 0, 1)
        struct.pack_into("<4I", data, 24, 0x0E8A0800, 1, DATA_OFFSET, len(bundle))
        data.extend(bundle)
        self.assertEqual(embedded_wave(data, 0x0E8A4ACA), wav)
        struct.pack_into("<I", data, DATA_OFFSET + 20, 0xFFFFFFFF)
        with self.assertRaises(ValueError):
            embedded_wave(data, 0x0E8A4ACA)




class ProjectedFloorBoundaryTests(unittest.TestCase):
    @staticmethod
    def floor(faces):
        vertices, triangles = [], []
        for face in faces:
            triangles.append(list(range(len(vertices), len(vertices) + 3)))
            vertices.extend(face)
        return {"vertices": vertices, "triangles": triangles}

    @staticmethod
    def projected(floor):
        return {tuple(sorted((a[::2], b[::2]))) for a, b in floor_boundary_edges(floor)}

    def test_split_and_partial_shared_edges(self):
        split = self.floor([[(0,0,0),(2,0,0),(0,0,2)],
                            [(0,0,0),(0,0,-1),(1,0,0)], [(1,0,0),(1,0,-1),(2,0,0)]])
        self.assertFalse(any(a[1] == b[1] == 0 for a, b in self.projected(split)))
        partial = self.floor([[(0,0,0),(3,0,0),(0,0,2)], [(1,0,0),(1,0,-2),(2,0,0)]])
        edges = self.projected(partial)
        self.assertIn(((0.,0.),(1.,0.)), edges)
        self.assertIn(((2.,0.),(3.,0.)), edges)
        self.assertNotIn(((1.,0.),(2.,0.)), edges)

    def test_interior_coverage_clips_only_the_covered_part(self):
        floor = self.floor([[(0,0,0),(4,0,0),(0,0,4)], [(0,0,-1),(4,0,-1),(0,0,1)]])
        edges = self.projected(floor)
        self.assertIn(((2.,0.),(4.,0.)), edges)
        self.assertFalse(any(a[1] == b[1] == 0 and a[0] < 1 < b[0] for a, b in edges))

    def test_native_floor_height_is_not_an_xz_layer_selector(self):
        adjacent = self.floor([[(0,0,0),(2,0,0),(0,0,2)], [(0,10,0),(0,10,-2),(2,10,0)]])
        self.assertFalse(any(a[1] == b[1] == 0 for a, b in self.projected(adjacent)))
        stacked = self.floor([[(0,0,0),(2,0,0),(0,0,2)], [(0,10,0),(2,10,0),(0,10,2)]])
        self.assertEqual(len(self.projected(stacked)), 3)
        self.assertEqual(stacked["vertices"][3][1], 10)

    def test_real_hole_retains_all_four_inner_edges(self):
        floor = self.floor([[(0,0,0),(4,0,0),(1,0,1)],[(4,0,0),(3,0,1),(1,0,1)],
                            [(0,0,0),(1,0,1),(0,0,4)],[(0,0,4),(1,0,1),(1,0,3)],
                            [(0,0,4),(1,0,3),(4,0,4)],[(4,0,4),(1,0,3),(3,0,3)],
                            [(4,0,0),(4,0,4),(3,0,1)],[(4,0,4),(3,0,3),(3,0,1)]])
        edges = self.projected(floor)
        for a, b in [((1.,1.),(3.,1.)),((1.,1.),(1.,3.)),((1.,3.),(3.,3.)),((3.,1.),(3.,3.))]:
            self.assertIn(tuple(sorted((a,b))), edges)

    def test_five_micrometre_overlap_and_gap_remain_distinct(self):
        epsilon = .000005
        north = [(0,0,0),(2,0,0),(0,0,2)]
        overlap = self.projected(self.floor([north,[(0,0,epsilon),(0,0,-2),(2,0,epsilon)]]))
        self.assertFalse(any(abs(a[1]) < .000006 and abs(b[1]) < .000006 and
                             min(a[0],b[0]) < 1 < max(a[0],b[0]) for a, b in overlap))
        gap = self.floor([north,[(0,0,-epsilon),(0,0,-2),(2,0,-epsilon)]])
        self.assertEqual(len(self.projected(gap)), 6)

    def test_unaffected_edge_order_and_endpoints_are_preserved(self):
        floor = self.floor([[(2,1,0),(0,0,0),(0,0,2)]])
        self.assertEqual(floor_boundary_edges(floor), [((0.,0.,0.),(2.,1.,0.)),
                         ((0.,0.,0.),(0.,0.,2.)),((0.,0.,2.),(2.,1.,0.))])

    @unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
    def test_original_ba11_both_source_covered_stair_seams(self):
        from export_episode import base, load_contexts
        if not (base.ROOT / "game/data/ba11.pc").is_file():
            self.skipTest("Original BA11 unavailable")
        contexts, _ = load_contexts(base.ROOT / "game", ("ba11",))
        floor = contexts["ba11"]["collision"]["floor"]
        packed = pack_floor(floor)
        edges = self.projected(floor)
        self.assertEqual(len(edges), 68)
        for z, x in [(-3.106019973754883,7.),(-3.2063450813293457,7.5),(-3.206350088119507,7.5)]:
            self.assertFalse(any(a[1] == b[1] == z and a[0] < x < b[0] for a, b in edges))
        self.assertEqual(pack_floor(floor), packed)


if __name__ == "__main__":
    unittest.main()
