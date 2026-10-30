"""Independently check a native walkthrough against the original PC scene."""

import argparse
import csv
import json
import math
from pathlib import Path

from ed_camera import parse_scene, view_projection
from ed_collision import parse_collision
from ed_triggers import parse_triggers, update_membership
from export_scene import ROOT, PLAYER_RADIUS, WALKTHROUGH_NODES, ground_axis, resource


def point_segment_distance(point, a, b):
    dx, dz = b[0] - a[0], b[1] - a[1]
    squared = dx * dx + dz * dz
    t = max(0, min(1, ((point[0] - a[0]) * dx + (point[1] - a[1]) * dz) / squared)) if squared else 0
    return math.hypot(point[0] - a[0] - dx * t, point[1] - a[1] - dz * t)


def cross(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def segment_distance(a, b, c, d):
    if (cross(a, b, c) * cross(a, b, d) <= 0
            and cross(c, d, a) * cross(c, d, b) <= 0
            and all(max(min(a[k], b[k]), min(c[k], d[k])) <= min(max(a[k], b[k]), max(c[k], d[k]))
                    for k in (0, 1))):
        return 0
    return min(point_segment_distance(a, c, d), point_segment_distance(b, c, d),
               point_segment_distance(c, a, b), point_segment_distance(d, a, b))


def inside_polygon(point, polygon):
    inside = False
    x, z = point
    for a, b in zip(polygon, polygon[1:] + polygon[:1]):
        if (a[1] > z) != (b[1] > z) and x < a[0] + (z - a[1]) * (b[0] - a[0]) / (b[1] - a[1]):
            inside = not inside
    return inside


def verify(trace, game):
    assert segment_distance((0, 0), (2, 0), (1, -1), (1, 1)) == 0
    assert segment_distance((0, 0), (2, 0), (0, 1), (2, 1)) == 1
    assert segment_distance((0, 0), (1, 0), (2, 0), (3, 0)) == 1
    assert inside_polygon((1, 1), [(0, 0), (2, 0), (2, 2), (0, 2)])

    payload = resource((game / "data/ak01.pc").read_bytes(), 0x0F0A0800)
    collision, scene, triggers = parse_collision(payload), parse_scene(payload), parse_triggers(payload)
    vertices = collision["floor"]["vertices"]
    height = vertices[0][1]
    if any(abs(point[1] - height) > 1e-6 for point in vertices):
        raise ValueError("Trace verifier requires the first scene's flat floor")
    min_x, max_x = min(p[0] for p in vertices), max(p[0] for p in vertices)
    min_z, max_z = min(p[2] for p in vertices), max(p[2] for p in vertices)
    polygons = [item["vertices_xz"] for item in collision["solids"]]
    route = [(scene["navpoints"][i][0], scene["navpoints"][i][2]) for i in WALKTHROUGH_NODES]
    bases = []
    for track in scene["tracks"]:
        camera = view_projection(track["frames"][0])
        right, forward = ground_axis(camera["right3"]), ground_axis(camera["forward3"])
        bases.append(((right[0], right[2]), (forward[0], forward[2])))
    previous = route[0]
    state = update_membership(triggers, [previous[0], height, previous[1]], current_camera=0)
    reference, basis = None, None
    waypoint, cuts, ticks = 0, 0, 0
    minimum_clearance = math.inf
    cameras = {state["camera"]}
    tolerance = 0.0002
    with trace.open(newline="", encoding="utf-8") as stream:
        for line, raw in enumerate(csv.DictReader(stream), 2):
            row = {key: float(value) for key, value in raw.items()}
            if not all(math.isfinite(value) for value in row.values()):
                raise ValueError(f"Trace line {line}: non-finite value")
            point = (row["x"], row["z"])
            if not (min_x + PLAYER_RADIUS - tolerance <= point[0] <= max_x - PLAYER_RADIUS + tolerance
                    and min_z + PLAYER_RADIUS - tolerance <= point[1] <= max_z - PLAYER_RADIUS + tolerance
                    and abs(row["y"] - height) < 1e-6):
                raise ValueError(f"Trace line {line}: actor left the source floor")
            for polygon in polygons:
                if inside_polygon(point, polygon):
                    raise ValueError(f"Trace line {line}: actor entered a solid")
                clearance = min(segment_distance(previous, point, a, b)
                                for a, b in zip(polygon, polygon[1:] + polygon[:1]))
                minimum_clearance = min(minimum_clearance, clearance)
                if clearance < PLAYER_RADIUS - tolerance:
                    raise ValueError(f"Trace line {line}: swept actor crossed a wall")

            stick = (row["stickX"], row["stickZ"])
            magnitude = math.hypot(*stick)
            if magnitude <= 0.001:
                reference, basis = None, None
                expected = (0, 0)
            else:
                direction = (stick[0] / magnitude, stick[1] / magnitude)
                if reference is None or sum(a * b for a, b in zip(reference, direction)) < 0.8660254:
                    reference, basis = direction, bases[state["camera"]]
                right, forward = basis
                world = (right[0] * stick[0] + forward[0] * stick[1],
                         right[1] * stick[0] + forward[1] * stick[1])
                world_length = math.hypot(*world)
                expected = tuple(value / world_length * row["requestedDistance"] for value in world)
            if math.hypot(expected[0] - row["desiredDX"], expected[1] - row["desiredDZ"]) > 1e-6:
                raise ValueError(f"Trace line {line}: movement changed with the camera basis")
            moved = math.dist(previous, point)
            if moved > row["requestedDistance"] + tolerance or abs(moved - row["movedDistance"]) > 1e-6:
                raise ValueError(f"Trace line {line}: unexpected displacement")
            next_state = update_membership(triggers, [point[0], height, point[1]],
                                           inside=state["inside"], current_camera=state["camera"])
            if next_state["camera"] != row["camera_index"]:
                raise ValueError(f"Trace line {line}: camera differs from the original trigger rules")
            cuts += next_state["camera"] != state["camera"]
            state = next_state
            cameras.add(state["camera"])
            if waypoint < len(route) and math.dist(point, route[waypoint]) < 0.03:
                waypoint += 1
            previous, ticks = point, ticks + 1
    if waypoint != len(route) or cuts < 3:
        raise ValueError("Trace did not complete the source route and camera transitions")
    return {"steps": ticks, "waypoints": waypoint, "camera_cuts": cuts,
            "visited_source_cameras": sorted(cameras), "minimum_swept_clearance": minimum_clearance,
            "radius": PLAYER_RADIUS, "camera_rules_match": True, "held_direction_match": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    args = parser.parse_args()
    try:
        print(json.dumps(verify(args.trace, args.game), indent=2))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
