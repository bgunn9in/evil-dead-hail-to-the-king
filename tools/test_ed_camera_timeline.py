from pathlib import Path
import struct
import unittest

from ed_camera_timeline import AC04, AD04, CameraTimeline, parse_timeline, read_scene, resolve_control
from test_ed_camera import make_scene


def tracks(fps=15):
    return [{"index": i, "background_start": i * 10,
             "frames": [{"index": frame} for frame in range(8)],
             "segments": [{"selectors": [0, 1, 3, 5], "fps": [fps, fps, fps]},
                          {"selectors": [3, 4, 6, 7], "fps": [fps, fps, fps]}]} for i in range(2)]


class TimelineTests(unittest.TestCase):
    def test_header_segments_are_not_frame_indices_or_speeds(self):
        raw = make_scene()
        struct.pack_into("<H4H", raw, 90, 1, 0, 0, 1, 1)
        raw[102:105] = bytes((1, 15, 7))
        parsed = parse_timeline(raw)
        self.assertEqual((parsed[0]["declared_segments"], len(parsed[0]["segments"])), (1, 5))
        self.assertEqual(parsed[0]["segments"][0]["selectors"], [0, 0, 1, 1])
        self.assertEqual(parsed[0]["segments"][0]["fps"], [1, 15, 7])
        self.assertEqual(resolve_control(parsed, 0, (4, 0, 0, 1, 2))["period"], 2)
        self.assertEqual(resolve_control(parsed, 0, (3, 0, 1, 1, 2))["frame"], 0)
        self.assertEqual(resolve_control(parsed, 0, (3, 0, 4, 1, 2))["frame"], 0)
        for segment in (-1, 5):
            with self.assertRaises(ValueError):
                resolve_control(parsed, 0, (3, 0, segment, 1, 2))
        parsed[0]["segments"][1]["selectors"][1] = 2
        with self.assertRaises(ValueError):
            resolve_control(parsed, 0, (3, 0, 1, 1, 2))

    def test_initial_tick_advances_and_loop_includes_end(self):
        timeline = CameraTimeline(tracks(), 0)
        timeline.control(4, 0)
        frames = [timeline.state["frame"]]
        for _ in range(7):
            timeline.tick(); frames.append(timeline.state["frame"])
        self.assertEqual(frames, [1, 2, 2, 3, 3, 1, 1, 2])
        self.assertEqual(sum(e["event"] == "callback" for e in timeline.events), 1)

    def test_hold_clamp_and_authored_fps_integer_period(self):
        for mode in (0, 1, 2, 3):
            timeline = CameraTimeline(tracks(30), 0); timeline.control(mode, 0)
            for _ in range(10):
                timeline.tick()
            self.assertEqual(timeline.state["frame"], 3 if mode in (0, 1) else 1)
            self.assertEqual(sum(e["event"] == "publish" for e in timeline.events), 11)
        for fps, period in ((0, 2), (7, 4), (15, 2), (30, 1), (255, 0)):
            self.assertEqual(resolve_control(tracks(fps), 0, (4, 0, 0, 1, 2))["period"], period)

    def test_pause_and_engine_mode_freeze_countdown(self):
        timeline = CameraTimeline(tracks(), 0); timeline.control(4, 0)
        self.assertIsNone(timeline.tick(paused=True))
        self.assertIsNone(timeline.tick(engine_mode=2))
        self.assertEqual((timeline.countdown, timeline.state["frame"]), (1, 1))
        self.assertEqual(timeline.tick()["frame"], 2)

    def test_control_keeps_countdown_and_identical_tuple_keeps_frame(self):
        timeline = CameraTimeline(tracks(7), 0); timeline.control(4, 0); timeline.tick(); timeline.tick()
        self.assertEqual(timeline.countdown, 3)
        self.assertFalse(timeline.control(4, 0))
        self.assertEqual((timeline.state["frame"], timeline.countdown), (2, 3))
        timeline.control(4, 1, 1)
        self.assertEqual((timeline.state["frame"], timeline.countdown), (4, 3))

    def test_callback_is_before_publication_and_nested_control_coalesces(self):
        timeline = CameraTimeline(tracks(), 0); timeline.control(4, 0)
        def callback(controller, camera, frame):
            self.assertEqual((camera, frame, controller.state["request"][1]), (1, 0, 0))
            controller.control(3, 0, 0)
            controller.control(4, 1, 1)
        timeline.control(4, 1, callback=callback)
        self.assertEqual(timeline.state["request"], (4, 1, 1, 1, 2))
        self.assertEqual([e["event"] for e in timeline.events[-4:]],
                         ["callback", "replace_pending", "replace_pending", "publish"])

    def test_inventory_mode3_holds_authored_selector_not_prior_render_frame(self):
        timeline = CameraTimeline(tracks(), 0); timeline.control(4, 0); timeline.tick()
        self.assertEqual(timeline.state["frame"], 2)
        timeline.control(3, 0)
        self.assertEqual(timeline.state["frame"], 1)

    def test_scene_specific_start_end_quirks_and_control_publish_gate(self):
        self.assertEqual(resolve_control(tracks(), AC04, (4, 0, 0, 3, 2))["start"], 0)
        self.assertEqual(resolve_control(tracks(), AC04, (4, 0, 0, 3, 2))["frame"], 5)
        self.assertEqual(resolve_control(tracks(), AD04, (4, 0, 0, 1, 2))["end"], 5)
        timeline = CameraTimeline(tracks(), 0); timeline.control(4, 0)
        timeline.publication_enabled = False
        self.assertFalse(timeline.control(3, 1))
        self.assertEqual(timeline.state["request"][1], 0)
        self.assertEqual(timeline.tick()["frame"], 2)  # Gate affects control, not timer41C530.

    @unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
    def test_original_ak01_ak09_and_segment_selection_goldens(self):
        game = Path(__file__).resolve().parents[1] / "game"
        if not (game / "data/ak01.pc").is_file():
            self.skipTest("Original user game not present")
        scene, original, _ = read_scene(game, "ak01")
        timeline = CameraTimeline(original, scene); timeline.control(4, 0)
        frames = [timeline.state["frame"]]
        for _ in range(31):
            timeline.tick(); frames.append(timeline.state["frame"])
        self.assertEqual(frames, [0] + [n for n in range(1, 15) for _ in range(2)] + [0, 0, 1])
        scene, original, _ = read_scene(game, "ak09")
        state = resolve_control(original, scene, (4, 8, 0, 1, 2))
        self.assertEqual((state["frame"], state["start"], state["end"], state["fps"], state["period"]), (15, 15, 28, 14, 2))
        scene, original, _ = read_scene(game, "ah10")
        self.assertEqual(resolve_control(original, scene, (4, 1, 1, 1, 2))["frame"], 15)
        self.assertEqual(resolve_control(original, scene, (3, 2, 2, 1, 1))["frame"], 2)

    @unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
    def test_original_main_callback_coalesces_ac02_progression_segment(self):
        from ed_scripts import ScriptPackage
        from ed_vm import Interpreter, Object
        game = Path(__file__).resolve().parents[1] / "game"
        if not (game / "data/ac02.pc").is_file():
            self.skipTest("Original user game not present")
        package = ScriptPackage((game / "data/script/pc/levelA.jpc").read_bytes())
        for progression, expected_frame in ((0, 0), (1, 15)):
            identity, original, _ = read_scene(game, "ac02")
            timeline = CameraTimeline(original, identity)
            def control(*args):
                timeline.control(*args)
            vm = Interpreter(package, natives={("ed/base/External", "controlCamera", "(IIIII)V"): control})
            scene = Object(vm.class_index("SceneAC02"), {44: 6, 45: 17})
            vm.statics[396] = Object(vm.class_index("ed/base/EDLevel"), {38: scene})
            vm.statics[426] = progression
            #Main816 -> virtual SceneAC02.reportCameraState1840 executes the
            #original mission predicate and requests segment0/1, no host branch.
            timeline.control(4, 6, callback=lambda _, camera, frame: vm.invoke(816, args=(camera, frame)))
            self.assertEqual(timeline.state["request"], (4, 6, progression, 1, 2))
            self.assertEqual(timeline.state["frame"], expected_frame)
            self.assertEqual([e["event"] for e in timeline.events], ["callback", "replace_pending", "publish"])
            timeline.tick()
            self.assertEqual((scene.fields[45], scene.fields[46]), (0, 17))

    @unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
    def test_original_bm01_open_bridge_uses_reserved_track0_segment1(self):
        from ed_scripts import ScriptPackage
        from ed_vm import Interpreter, Object
        game = Path(__file__).resolve().parents[1] / "game"
        if not (game / "data/bm01.pc").is_file():
            self.skipTest("Original user game not present")
        identity, original, _ = read_scene(game, "bm01")
        self.assertEqual((original[0]["declared_segments"], len(original[0]["frames"])), (1, 1))
        timeline = CameraTimeline(original, identity)
        package = ScriptPackage((game / "data/script/pc/levelB.jpc").read_bytes())
        def control(*args):
            timeline.control(*args)
        vm = Interpreter(package, natives={("ed/base/External", "controlCamera", "(IIIII)V"): control})
        scene = Object(vm.class_index("SceneBM01"), {44: 0, 45: 17})
        vm.statics[396] = Object(vm.class_index("ed/base/EDLevel"), {38: scene})
        vm.statics[531] = 1
        timeline.control(4, 0, callback=lambda _, camera, frame:
                         vm.invoke(vm.method_index("Main", "cameraUpdate", "(II)V"), args=(camera, frame)))
        self.assertEqual(timeline.state["request"], (3, 0, 1, 1, 2))
        self.assertEqual((timeline.state["frame"], timeline.state["fps"], timeline.state["period"]), (0, 0, 2))
        for _ in range(8):
            timeline.tick()
            self.assertEqual(timeline.state["frame"], 0)


if __name__ == "__main__":
    unittest.main()
