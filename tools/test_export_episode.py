import copy
import json
from pathlib import Path
import struct
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch
import export_episode as episode_exporter

from export_episode import (SCENES, LEGACY_SCENES, arrival_spawn, base, load_contexts, pack_portals,
                            portal_manifest, spawn_clearance, levela_coverage, source_dependencies, cached_levelb_rooms)


def fixtures():
    def portal(index, target, shape=0):
        points = [[0., 0., 0.], [2., 0., 2.]] if shape == 0 else [[0., 0., 0.], [0., 0., 2.], [2., 0., 2.], [2., 0., 0.]]
        return {"index": index, "mode": 0, "shape": shape, "points": points,
                "spatial_mask": 0x10001, "destination_scene_id": target,
                "destination_scene": "bb01" if target == 0x62620100 else "cc01",
                "camera_index": 1, "marker_index": 0, "rotation_xyz": [0., 1., 0.], "yaw": 1.}
    parsed = {"scene_id": 0x61610100, "scene_name": "aa01",
              "spatial_grid": {"grid_origin_xz": [0., 0.], "grid_extent_xz": [10., 10.]},
              "portals": [{**portal(0, 0x62620100), "mode": 1},
                          portal(2, 0x62620100), portal(5, 0x63630100, 2)]}
    destination = {"scene_id": 0x62620100, "scene_name": "bb01", "markers": [[3., 7., 5.]], "camera_count": 2}
    return parsed, {destination["scene_id"]: destination}


class PortalManifestTests(unittest.TestCase):
    def test_original_order_geometry_and_marker_y_are_preserved(self):
        parsed, targets = fixtures()
        manifest = portal_manifest(parsed, targets)
        data = pack_portals(manifest)
        self.assertEqual(struct.unpack_from("<8s4sI4f", data), (b"EDPOR1\0\0", b"aa01", 2, 0., 0., 10., 10.))
        self.assertEqual(struct.unpack_from("<7I4s", data, 32), (2, 0, 2, 0x10001, 0x62620100, 1, 0, b"bb01"))
        self.assertEqual(struct.unpack_from("<4f", data, 64), (0., 0., 2., 2.))
        self.assertEqual(struct.unpack_from("<3fI3f", data, 80), (0., 1., 0., 1, 3., 7., 5.))
        self.assertEqual(manifest["portals"][0]["transition"]["spawn_xyz"], [3., 0., 5.])

    def test_unavailable_destination_has_metadata_and_no_fabricated_pose(self):
        parsed, targets = fixtures()
        manifest = portal_manifest(parsed, targets)
        record = manifest["portals"][1]
        self.assertEqual(record["destination_scene"], "cc01")
        self.assertIsNone(record["transition"])
        self.assertFalse(record["destination_available"])
        data = pack_portals(manifest)
        self.assertEqual(len(data), 32 + 76 + 80)
        self.assertEqual(struct.unpack_from("<I", data, len(data) - 4)[0], 0)

    def test_changed_target_bounds_and_duplicate_source_portal_fail(self):
        parsed, targets = fixtures()
        targets[0x62620100]["camera_count"] = 1
        with self.assertRaises(ValueError):
            portal_manifest(parsed, targets)
        parsed, targets = fixtures()
        manifest = portal_manifest(parsed, targets)
        manifest["portals"].append(copy.deepcopy(manifest["portals"][0]))
        with self.assertRaises(ValueError):
            pack_portals(manifest)

    def test_clearance_uses_analytic_circle_and_source_floor_edges(self):
        floor = {"vertices": [[0., 0., 0.], [0., 0., 10.], [10., 0., 10.], [10., 0., 0.]],
                 "triangles": [[0, 1, 2], [0, 2, 3]]}
        collision = {"floor": floor, "solids": [{"shape": 3, "circle_xz": {"center": [5., 5.], "radius": 1.}}]}
        self.assertEqual(spawn_clearance(collision, 5., 5.), -1.)
        self.assertEqual(spawn_clearance(collision, 5., 7.), 1.)
        self.assertEqual(spawn_clearance(collision, .1, 5.), .1)
        collision["solids"].append({"shape": 0, "vertices_xz": [[7.,1.],[7.,3.],[9.,3.],[9.,1.]]})
        boundary = [(a[::2], b[::2]) for a, b in base.floor_boundary_edges(floor)]
        expected = {point: spawn_clearance(collision, *point) for point in [(5.,5.), (5.,7.), (.1,5.), (8.,2.)]}
        with patch.object(base, "floor_boundary_edges", side_effect=AssertionError("Repeated floor derivation")):
            for point, value in expected.items():
                self.assertEqual(spawn_clearance(collision, *point, floor_edges=boundary), value)

    @unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
    def test_actual_eleven_scenes_preserve_eight_scene_subset_and_arrivals(self):
        contexts, destinations = load_contexts(base.ROOT / "game", LEGACY_SCENES)
        self.assertEqual(tuple(contexts), LEGACY_SCENES)
        manifests = {name: portal_manifest(context["portals"], destinations) for name, context in contexts.items()}
        self.assertEqual(sum(len(item["portals"]) for item in manifests.values()), 39)
        self.assertEqual(sum(p["destination_available"] for item in manifests.values() for p in item["portals"]), 33)
        baseline = {context["id"]: context["portals"] for name, context in contexts.items() if name in LEGACY_SCENES[:8]}
        original = [portal_manifest(contexts[name]["portals"], baseline) for name in LEGACY_SCENES[:8]]
        self.assertEqual(sum(len(item["portals"]) for item in original), 30)
        self.assertEqual(sum(p["destination_available"] for item in original for p in item["portals"]), 23)
        for name in LEGACY_SCENES[2:]:
            position, source = arrival_spawn(name, contexts, destinations)
            marker = contexts[name]["scene"]["markers"][source["marker_index"]]
            self.assertEqual(position[::2], marker[::2])
            self.assertGreaterEqual(source["static_clearance"] + 1e-6, base.PLAYER_RADIUS)
            self.assertEqual(position[1], base.source_floor_height(contexts[name]["collision"]["floor"], *position[::2]))

    @unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
    def test_supported_levela_selection_keeps_real_unsupported_and_inactive_namespace_separate(self):
        coverage = levela_coverage(base.ROOT / "game")
        self.assertEqual(len(SCENES), 32)
        self.assertEqual(SCENES[:11], LEGACY_SCENES)
        self.assertEqual({row["scene"] for row in coverage["scenes"] if not row["included"]},
                         {"aa01"})
        at01 = next(row for row in coverage["scenes"] if row["scene"] == "at01")
        self.assertEqual(at01["actor_prototypes"], [16])
        self.assertTrue(at01["included"])
        self.assertNotIn(16, coverage["supported_native_types"])
        self.assertTrue(at01["notes"])

    @unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
    def test_full_levela_authored_arrivals_and_triangle_preloads(self):
        game = base.ROOT / "game"
        coverage, dependencies = levela_coverage(game), source_dependencies(game, SCENES)
        contexts, destinations = load_contexts(game)
        names = [*SCENES, *(row["scene"] for row in coverage["scenes"] if row["scene"] not in SCENES)]
        _, sources = load_contexts(game, names)
        for name in SCENES[2:]:
            position, provenance = arrival_spawn(name, contexts, sources, dependencies["literal_scene_switches"])
            self.assertEqual(position[::2], contexts[name]["scene"]["markers"][provenance["marker_index"]][::2])
            self.assertGreater(provenance["static_clearance"], 0)
        _, narrow = arrival_spawn("ac14", contexts, sources, dependencies["literal_scene_switches"])
        self.assertFalse(narrow["modern_clearance_satisfied"])
        self.assertIn("retained", narrow["clearance_note"])
        _, isolated = arrival_spawn("au01", contexts, sources)
        self.assertEqual(isolated["source_scene"], "ag03")
        self.assertTrue(isolated["source_scene_available"])
        preloads = [p for p in contexts["ah05"]["portals"]["portals"] if p["mode"] == 1 and p["shape"] == 1]
        self.assertEqual(len(preloads), 1)
        self.assertEqual(len(preloads[0]["points"]), 3)
        self.assertNotIn(preloads[0]["index"], [p["index"] for p in portal_manifest(contexts["ah05"]["portals"], destinations)["portals"]])
        self.assertTrue(any(movie["id"] == 0x61303600 for movie in dependencies["movies"]))


class LevelBIncrementalTests(unittest.TestCase):
    def fixture(self, directory):
        root = Path(directory)
        context = {"bm01": {"id": 0x626D0100, "payload": b"source-native", "portals": {},
                              "scene": {"tracks": [{"frames": [0, 1]}]},
                              "collision": {"solids": [], "floor": {"vertices": [[0,0,0],[0,0,1],[1,0,0]],
                                                                        "triangles": [[0,1,2]]}}}}
        (root / "episode.json").write_text(json.dumps({"level": "B", "scenes": ["bm01"],
                                                       "source_script_sha256": "original-script"}))
        room = root / "bm01"
        room.mkdir()
        files = ("scene.txt", "collision.bin", "floor.bin", "triggers.bin", "camera-frames.bin", "doors.bin",
                 "ash.bmp", "ash.bin", "idle.bin", "walk.bin", "font.bmp", "font.txt", "door.wav",
                 "background_0.bmp", "background_0.depth", "background_0_1.bmp", "background_0_1.depth")
        for name in files:
            (room / name).write_bytes(b"existing-export")
        (room / "collision.bin").write_bytes(base.pack_collision(context["bm01"]["collision"])[0])
        (room / "scene.native").write_bytes(b"source-native")
        (room / "source.json").write_text(json.dumps({"scene": "bm01", "scene_id": 0x626D0100}))
        return root, room, context

    def test_retain_complete_rooms_and_repair_missing_authored_frame(self):
        with TemporaryDirectory() as temporary:
            root, room, contexts = self.fixture(temporary)
            before = {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in room.iterdir()}
            self.assertEqual(cached_levelb_rooms(root, ("bm01", "bm02"), contexts, "original-script"), {"bm01"})
            self.assertEqual(before, {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in room.iterdir()})
            (room / "background_0_1.depth").unlink()
            self.assertEqual(cached_levelb_rooms(root, ("bm01", "bm02"), contexts, "original-script"), set())

    def test_reject_mixed_package_or_changed_original_scene_before_writes(self):
        with TemporaryDirectory() as temporary:
            root, room, contexts = self.fixture(temporary)
            with self.assertRaises(ValueError):
                cached_levelb_rooms(root, ("bm01",), contexts, "other-script")
            (room / "scene.native").write_bytes(b"different-original")
            with self.assertRaises(ValueError):
                cached_levelb_rooms(root, ("bm01",), contexts, "original-script")

    def test_valid_but_stale_derived_collision_is_dirty_without_source_changes(self):
        with TemporaryDirectory() as temporary:
            root, room, contexts = self.fixture(temporary)
            current = (room / "collision.bin").read_bytes()
            stale = bytearray(current)
            struct.pack_into("<f", stale, 24, .125)  # Still a finite, non-degenerate EDCOLL1 triangle.
            (room / "collision.bin").write_bytes(stale)
            before = {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in room.iterdir()}
            self.assertEqual(cached_levelb_rooms(root, ("bm01",), contexts, "original-script"), set())
            self.assertEqual(before, {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in room.iterdir()})
            (room / "collision.bin").write_bytes(current)
            self.assertEqual(cached_levelb_rooms(root, ("bm01",), contexts, "original-script"), {"bm01"})

    def test_extend_regenerates_dirty_room_and_refreshes_root_alias(self):
        # Isolate orchestration/media writers; use the real cache predicate,
        # real collision packer, real directory/root-alias copying and metadata.
        with TemporaryDirectory() as temporary:
            root, room, contexts = self.fixture(temporary)
            game = root / "original-fixture"
            game.mkdir()
            (root / "scripts").mkdir()
            (game / "EvilDead.exe").write_bytes(b"fixture-exe")
            (room / "collision.bin").write_bytes(b"stale-derived-payload")
            calls = []
            def regenerate(game, output, name, context, actor, shared_output, doors, **kwargs):
                calls.append(name)
                (output / "collision.bin").write_bytes(base.pack_collision(context["collision"])[0])
            coverage = {"source_script_sha256": "original-script", "scenes": [{"scene": "bm01", "included": True}]}
            with patch.multiple(episode_exporter, levelb_coverage=lambda *_: coverage,
                    source_dependencies=lambda *a, **k: {"literal_scene_switches": [], "movies": []},
                    load_contexts=lambda *a, **k: (contexts, {}), read_depth_patches=lambda *_: {},
                    arrival_spawn=lambda *a, **k: ([0,0,0], {"role": "synthetic cache fixture"}),
                    portal_manifest=lambda *a, **k: {"portals": []}, pack_portals=lambda *_: b"portal-fixture",
                    export_runtime_movies=lambda *a, **k: [], export_runtime_objects=lambda *a, **k: {"models": [], "textures": []},
                    export_runtime_vfx=lambda *a, **k: None), \
                    patch.object(base, "export_shared_assets", return_value=({}, None)), \
                    patch.object(base, "export_location", side_effect=regenerate):
                result = episode_exporter.export_levelb_episode(game, root, extend=True)
            self.assertEqual(calls, ["bm01"])
            self.assertEqual(result["incremental_exported"], ["bm01"])
            self.assertEqual(result["incremental_preserved"], [])
            self.assertEqual((root / "collision.bin").read_bytes(), base.pack_collision(contexts["bm01"]["collision"])[0])
            self.assertEqual((root / "scene.native").read_bytes(), b"source-native")

    def test_clean_nested_output_needs_no_existing_manifest(self):
        with TemporaryDirectory() as temporary:
            self.assertEqual(cached_levelb_rooms(Path(temporary) / "assets/levelB", ("bm01",), {}, "original-script"), set())


if __name__ == "__main__":
    unittest.main()
