"""Evaluate the disassembled PC camera controller without running game code."""

import argparse
import hashlib
import json
from pathlib import Path
import struct

from ed_assets import parse_archive
from ed_camera import parse_scene


AC04 = 0x61630400
AD04 = 0x61640400


def parse_timeline(payload):
    """Keep authored segment endpoints/FPS alongside the original frame records."""
    tracks = parse_scene(payload)["tracks"]
    for track in tracks:
        offset = track["offset"]
        count = struct.unpack_from("<H", payload, offset + 2)[0]
        if not 1 <= count <= 5:
            raise ValueError("Unsupported camera segment count")
        track["declared_segments"] = count
        track["segments"] = []
        #417740 directly indexes the fixed five slots, including reserved records.
        for index in range(5):
            start = offset + index * 16
            track["segments"].append({
                "index": index,
                "selectors": list(struct.unpack_from("<4H", payload, start + 4)),
                "fps": list(payload[start + 14:start + 17]),
                "source_offset": start + 4,
            })
    return tracks


def resolve_control(tracks, scene_id, request):
    """417740: mode, track, segment, start-selector, end-selector.

    This checked evaluator rejects invalid indices; the native function does
    not bounds-check them. It implements only the five decoded modes0..4.
    """
    mode, camera, segment, selector_a, selector_b = request
    if mode not in range(5) or not 0 <= camera < len(tracks):
        raise ValueError("Unsupported camera mode/track")
    track = tracks[camera]
    if not 0 <= segment < min(5, len(track["segments"])) or selector_a not in range(4) or selector_b not in range(4):
        raise ValueError("Unsupported camera segment/selector")
    record = track["segments"][segment]
    selected = record["selectors"][selector_a]
    #41792E..417936 overrides the temporary AC04 range start with literal0.
    start = 0 if scene_id == AC04 else selected
    end = record["selectors"][3 if scene_id == AD04 else selector_b]
    fps = record["fps"][min(selector_a, 2)]
    period = 30 // fps if fps else 2
    if any(value >= len(track["frames"]) for value in (selected, start, end)):
        raise ValueError("Authored camera endpoint is outside frame data")
    return {"request": tuple(request), "frame": selected, "start": start, "end": end,
            "fps": fps, "period": period}


class CameraTimeline:
    """Original per-scene countdown and synchronous control-call coalescing.

    callback(controller, requested_track, 0) may call control() again; the last
    nested request replaces the pending tuple without another callback.
    tick() is one416320 scene worker call, not an elapsed-time interpolation.
    """
    def __init__(self, tracks, scene_id):
        self.tracks, self.scene_id = tracks, scene_id
        self.state = None
        self.countdown = 1  #415D19 resets on scene initialization, not camera change.
        self.pending = None
        self.publication_enabled = True  #417580(1) clears native427F82C.
        self.events = []

    def control(self, mode, track, segment=0, selector_a=1, selector_b=2, callback=None):
        request = (mode, track, segment, selector_a, selector_b)
        if any(not isinstance(value, int) for value in request):
            raise ValueError("Camera control arguments must be integers")
        if self.pending is not None:
            self.pending = request
            self.events.append({"event": "replace_pending", "request": list(request)})
            return False
        self.pending = request
        self.events.append({"event": "callback", "track": track, "reported_frame": 0,
                            "published_track_before": self.state["request"][1] if self.state else None})
        if callback is not None:
            callback(self, track, 0)
        request, self.pending = self.pending, None
        if self.state is not None and request == self.state["request"]:
            return False  #Main callback already ran, but no frame/countdown reset.
        candidate = resolve_control(self.tracks, self.scene_id, request)
        if not self.publication_enabled:
            self.events.append({"event": "control_suppressed", "request": list(request)})
            return False
        self.state = candidate
        self._publish("control")
        return True

    def _publish(self, reason):
        track = self.tracks[self.state["request"][1]]
        frame = self.state["frame"]
        if not 0 <= frame < len(track["frames"]):
            raise ValueError("Camera worker selected an unavailable frame")
        result = {"event": "publish", "reason": reason, "track": track["index"], "frame": frame,
                  "background_index": track["background_start"] + frame}
        self.events.append(result)
        return result

    def tick(self, *, engine_mode=0, paused=False):
        if self.state is None or engine_mode != 0 or paused:
            return None
        self.countdown -= 1
        if self.countdown > 0:
            return None
        self.countdown = self.state["period"]
        mode = self.state["request"][0]
        candidate = self.state["frame"] + 1
        if mode in (0, 1):
            candidate = min(candidate, self.state["end"])
        elif mode in (2, 3):
            candidate = self.state["frame"]
        elif candidate > self.state["end"]:
            candidate = self.state["start"]
        self.state["frame"] = candidate
        return self._publish("timer")  #41C530 runs even if held frame is unchanged.

    def frame_record(self):
        if self.state is None:
            raise ValueError("No camera has been published")
        return self.tracks[self.state["request"][1]]["frames"][self.state["frame"]]


def read_scene(game, name):
    path = Path(game) / "data" / (name + ".pc")
    archive = path.read_bytes()
    entries = [entry for entry in parse_archive(archive) if entry.resource_id >> 24 == 15]
    if len(entries) != 1:
        raise ValueError("Expected one original scene resource")
    entry = entries[0]
    payload = archive[entry.offset:entry.offset + entry.size]
    return struct.unpack_from("<I", archive)[0], parse_timeline(payload), {
        "archive": str(path), "archive_sha256": hashlib.sha256(archive).hexdigest(),
        "entry": entry.index, "payload_offset": entry.offset,
        "payload_sha256": hashlib.sha256(payload).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=Path(__file__).resolve().parents[1] / "game")
    parser.add_argument("--scene", default="ak01")
    parser.add_argument("--track", type=int, default=0)
    parser.add_argument("--segment", type=int, default=0)
    parser.add_argument("--mode", type=int, default=4)
    parser.add_argument("--ticks", type=int, default=34)
    args = parser.parse_args()
    if not 0 <= args.ticks <= 10000:
        parser.error("ticks must be0..10000")
    scene, tracks, source = read_scene(args.game, args.scene)
    timeline = CameraTimeline(tracks, scene)
    timeline.control(args.mode, args.track, args.segment)
    for _ in range(args.ticks):
        timeline.tick()
    print(json.dumps({"source": source, "state": timeline.state, "countdown": timeline.countdown,
                      "events": timeline.events}, indent=2))


if __name__ == "__main__":
    main()
