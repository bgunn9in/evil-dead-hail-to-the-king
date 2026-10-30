"""Read-only geometry certificate for the actual AH05 return-walk stall."""
import argparse
import hashlib
import json
import math
import struct
from pathlib import Path

from ed_assets import parse_archive
from ed_collision import parse_collision
from ed_actor_collision import static_class_accept

EXE_SHA = "62ae3c5512fb164e64f2c4375b67ae8c38f7c0da8219f4b41f8aa9b3a4ca862f"
SCENE_SHA = "a07767579482864f52abfc7fc721f3d726a60ba398fb77427fbae41f1ddf896f"


def _near(p, a, b):
    dx, dz = b[0] - a[0], b[1] - a[1]
    size = dx * dx + dz * dz
    t = max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dz) / size)) if size else 0.0
    return a[0] + t * dx, a[1] + t * dz


def _edges(poly):
    return zip(poly, poly[1:] + poly[:1])


def _inside(p, poly):
    return all((b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]) < 0 for a, b in _edges(poly))


def polygon_gap(first, second):
    candidates = []
    for p in first:
        for a, b in _edges(second):
            q = _near(p, a, b)
            candidates.append((math.dist(p, q), p, q))
    for p in second:
        for a, b in _edges(first):
            q = _near(p, a, b)
            candidates.append((math.dist(p, q), q, p))
    return min(candidates, key=lambda item: item[0])


def _segment_distance(a, b, c, d):
    u, v = (b[0] - a[0], b[1] - a[1]), (d[0] - c[0], d[1] - c[1])
    cross = u[0] * v[1] - u[1] * v[0]
    if cross:
        w = c[0] - a[0], c[1] - a[1]
        t = (w[0] * v[1] - w[1] * v[0]) / cross
        q = (w[0] * u[1] - w[1] * u[0]) / cross
        if 0 <= t <= 1 and 0 <= q <= 1:
            return 0.0
    return min(math.dist(a, _near(a, c, d)), math.dist(b, _near(b, c, d)),
               math.dist(c, _near(c, a, b)), math.dist(d, _near(d, a, b)))


def segment_clearance(a, b, polygons):
    if any(_inside(a, p) or _inside(b, p) for p in polygons):
        return 0.0
    return min(_segment_distance(a, b, c, d) for p in polygons for c, d in _edges(p))


def saved_prefix(data):
    """Only the bounded EDPLAY1/v17 prefix needed by this captured case."""
    cursor = 0

    def take(fmt):
        nonlocal cursor
        size = struct.calcsize("<" + fmt)
        if cursor + size > len(data):
            raise ValueError("Truncated saved prefix")
        values = struct.unpack_from("<" + fmt, data, cursor)
        cursor += size
        return values[0] if len(values) == 1 else values

    def text():
        nonlocal cursor
        count = take("I")
        if count > 4096 or cursor + count > len(data):
            raise ValueError("Invalid saved string")
        value = data[cursor:cursor + count].decode("ascii")
        cursor += count
        return value

    def flags():
        nonlocal cursor
        count = take("I")
        if count > 100000 or cursor + count > len(data):
            raise ValueError("Invalid saved flags")
        values = list(data[cursor:cursor + count])
        cursor += count
        if any(v not in (0, 1) for v in values):
            raise ValueError("Invalid saved boolean")
        return values

    if text() != "EDPLAY1" or take("I") != 17 or text() != "ah05" or take("B") != 1:
        raise ValueError("Only the captured original AH05/v17 prefix is supported")
    x, y, z, yaw, offset, radius = take("6f")
    camera, triangle = take("2I")
    animation, walking = take("d"), take("B")
    flags()
    tick, fraction, clock, guard, pending = take("Q"), take("d"), take("d"), take("I"), take("i")
    if walking > 1 or radius < 0 or not all(math.isfinite(v) for v in (x, y, z, yaw, offset, radius, animation, fraction, clock)):
        raise ValueError("Invalid saved actor prefix")
    count = take("I")
    if count > 100000:
        raise ValueError("Excessive saved gates")
    for _ in range(count):
        on, waiting, _ = take("BBI")
        if on > 1 or waiting > 1:
            raise ValueError("Invalid saved gate flags")
    enabled = flags()
    if len(enabled) <= 53:
        raise ValueError("Captured AH05 throat volumes are missing")
    return {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data), "pose": [x, y, z, yaw],
            "floor_offset": offset, "radius": radius, "camera": camera, "floor_triangle": triangle,
            "animation_time": animation, "walking": bool(walking), "source_tick": tick,
            "tick_fraction": fraction, "clock": clock, "guard": guard, "pending": pending,
            "enabled_throat_volumes": {str(i): bool(enabled[i]) for i in (0, 42, 43, 52, 53)}}


def source_case(game, saved_world=None):
    game = Path(game)
    if hashlib.sha256((game / "EvilDead.exe").read_bytes()).hexdigest() != EXE_SHA:
        raise ValueError("This certificate requires the verified original PC executable")
    archive = (game / "data/ah05.pc").read_bytes()
    matches = [(i, e) for i, e in enumerate(parse_archive(archive)) if e.resource_id == 0x0F072800]
    if len(matches) != 1:
        raise ValueError("Original AH05 scene resource is missing or ambiguous")
    entry_index, entry = matches[0]
    payload = archive[entry.offset:entry.offset + entry.size]
    if hashlib.sha256(payload).hexdigest() != SCENE_SHA:
        raise ValueError("Unknown AH05 source scene version")
    collision = parse_collision(payload)
    solids = {s["volume_index"]: s for s in collision["solids"]}
    selected = {i: solids[i]["vertices_xz"] for i in (0, 42, 43, 52, 53)}
    if not all(static_class_accept(solids[i]["flags_raw"], 4, 1, solids[i]["shape"]) for i in selected):
        raise ValueError("AH05 throat is not eligible for the original hero static query")
    width, first, second = polygon_gap(selected[42], selected[43])
    rect_width, rect_first, rect_second = polygon_gap(selected[52], selected[53])
    nav18 = collision["navigation"]["points"][18]
    point = nav18[0], nav18[2]
    point_radius = min(math.dist(point, _near(point, a, b)) for p in selected.values() for a, b in _edges(p))
    witness = ((-21.105, -7.4), (-21.105, -10.4))
    witness_radius = segment_clearance(*witness, list(selected.values()))
    result = {
        "format": "AH05-STATIC-POINT-CERTIFICATE1", "original_executable_run": False,
        "source": {"archive": "game/data/ah05.pc", "resource_id": "0f072800", "entry": entry_index,
                   "payload_bytes": len(payload), "scene_sha256": SCENE_SHA, "exe_sha256": EXE_SHA},
        "volumes": {str(i): {"vertices_xz": selected[i], "flags_raw": solids[i]["flags_raw"]} for i in selected},
        "rectangle52_53": {"gap_m": rect_width, "radius_upper_bound_m": rect_width / 2,
                           "closest": [rect_first, rect_second]},
        "entrance42_43": {"gap_m": width, "radius_upper_bound_m": width / 2,
                          "closest": [first, second]},
        "original_nav18": {"xz": point, "static_clearance_m": point_radius, "point_clear": not any(_inside(point, p) for p in selected.values())},
        "vertical_point_witness": {"segment_xz": witness, "clearance_m": witness_radius,
                                   "point_clear": witness_radius > 0, "radius_017_clear": witness_radius > .17},
        "modern_radius_025_fits_throat": width >= .5,
        "scope": "Local authored throat certificate, not a claim that every possible path through the whole scene was searched.",
    }
    if saved_world is not None:
        result["captured_runtime_prefix"] = saved_prefix(Path(saved_world).read_bytes())
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=Path(__file__).resolve().parents[1] / "game")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--saved-world", type=Path)
    args = parser.parse_args()
    result = source_case(args.game, args.saved_world)
    encoded = json.dumps(result, indent=2) + "\n"
    if args.output:
        target, game = args.output.resolve(), args.game.resolve()
        if target == game or game in target.parents:
            raise ValueError("Cannot write a certificate into original game files")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(encoded, encoding="utf-8")
    else:
        print(encoded, end="")


if __name__ == "__main__":
    main()
