"""Verify walking legs of the real starter-cord episode against source geometry.

Waypoints prefer original navigation points, with collision-derived planning for
player-only branches. Portal targets come from original plates. This verifies
static clearance, not NPC AI or automatic passage unlocks. Pickup/use predicates
execute in jpc_cord_door_case.
"""

import argparse
import hashlib
import heapq
import json
import math
from pathlib import Path

from export_episode import _edge_distance, load_contexts, spawn_clearance
from export_scene import ROOT, floor_boundary_edges, source_floor_height
from ed_triggers import contains
from jpc_scene_activation_case import SceneActivationCase


RADIUS = .25
CORD_PICKUP = (-28.650100708007812, 78.38780212402344)
CORD_USE = (-16.2990555763, 1.0646858811)


def crossing(a, b, c, d):
    rx, rz, sx, sz = b[0]-a[0], b[1]-a[1], d[0]-c[0], d[1]-c[1]
    determinant = rx*sz-rz*sx
    if determinant == 0:
        return False
    qx, qz = c[0]-a[0], c[1]-a[1]
    t, u = (qx*sz-qz*sx)/determinant, (qx*rz-qz*rx)/determinant
    return 0 <= t <= 1 and 0 <= u <= 1


def segment_distance(a, b, c, d):
    if crossing(a, b, c, d):
        return 0.0
    return min(_edge_distance(*a, c, d), _edge_distance(*b, c, d),
               _edge_distance(*c, a, b), _edge_distance(*d, a, b))


def route_between(context, start, goal):
    collision = context["collision"]
    boundary = [(a[::2], b[::2]) for a, b in floor_boundary_edges(collision["floor"])]
    edges, circles = list(boundary), []
    for solid in collision["solids"]:
        if solid["shape"] == 3:
            circles.append(solid["circle_xz"])
        else:
            points = solid["vertices_xz"]
            edges.extend(zip(points, points[1:] + points[:1]))

    def clearance(a, b, threshold=None):
        distance = math.inf
        for c, d in edges:
            if threshold is not None and (max(a[0], b[0])+threshold < min(c[0], d[0]) or
                    min(a[0], b[0])-threshold > max(c[0], d[0]) or
                    max(a[1], b[1])+threshold < min(c[1], d[1]) or
                    min(a[1], b[1])-threshold > max(c[1], d[1])):
                continue
            distance = min(distance, segment_distance(a, b, c, d))
            if threshold is not None and distance < threshold-1e-6:
                return distance
        for circle in circles:
            distance = min(distance, _edge_distance(*circle["center"], a, b)-circle["radius"])
        return distance

    points = [point[::2] for point in collision["navigation"]["points"]]+[list(start), list(goal)]
    count = len(points)
    valid = [spawn_clearance(collision, *point, floor_edges=boundary) >= RADIUS-1e-6 for point in points]
    for point in points[-2:]:
        source_floor_height(collision["floor"], *point)
    if not all(valid[-2:]):
        raise ValueError("Source route endpoint has insufficient modern actor clearance")
    graph = [[] for _ in points]
    for i in range(count):
        if not valid[i]:
            continue
        for j in range(i+1, count):
            if valid[j] and clearance(points[i], points[j], RADIUS) >= RADIUS-1e-6:
                length = math.dist(points[i], points[j])
                graph[i].append((j, length))
                graph[j].append((i, length))
    pending, best, previous = [(0.0, count-2)], {count-2: 0.0}, {}
    while pending:
        distance, index = heapq.heappop(pending)
        if index == count-1:
            break
        if distance > best[index]:
            continue
        for target, length in graph[index]:
            candidate = distance+length
            if candidate < best.get(target, math.inf):
                best[target], previous[target] = candidate, index
                heapq.heappush(pending, (candidate, target))
    if count-1 not in best:
        # Some source rooms have no NPC navigation points on their player-only
        # branches. Plan derived waypoints over the same immutable geometry.
        step = .4
        origin = collision["floor"]["bounds_min"][::2]
        extent = [a-b for a, b in zip(collision["floor"]["bounds_max"][::2], origin)]
        maximum = [math.ceil(value/step) for value in extent]
        grid_point = lambda key: [origin[0]+key[0]*step, origin[1]+key[1]*step]
        valid_cache = {}
        def grid_valid(key):
            if key not in valid_cache:
                point = grid_point(key)
                try:
                    source_floor_height(collision["floor"], *point)
                    valid_cache[key] = spawn_clearance(collision, *point, floor_edges=boundary) >= RADIUS-1e-6
                except ValueError:
                    valid_cache[key] = False
            return valid_cache[key]
        nearest = [round((value-base)/step) for value, base in zip(start, origin)]
        pending, grid_best, grid_previous = [], {}, {}
        for dx in range(-2, 3):
            for dz in range(-2, 3):
                key = (nearest[0]+dx, nearest[1]+dz)
                if grid_valid(key) and clearance(start, grid_point(key), RADIUS) >= RADIUS-1e-6:
                    distance = math.dist(start, grid_point(key))
                    grid_best[key], grid_previous[key] = distance, None
                    heapq.heappush(pending, (distance+math.dist(grid_point(key), goal), distance, key))
        found = None
        while pending:
            _, distance, key = heapq.heappop(pending)
            if distance > grid_best[key]:
                continue
            point = grid_point(key)
            if math.dist(point, goal) < step*3 and clearance(point, goal, RADIUS) >= RADIUS-1e-6:
                found = key
                break
            for dx, dz in ((-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)):
                target = (key[0]+dx, key[1]+dz)
                if not 0 <= target[0] <= maximum[0] or not 0 <= target[1] <= maximum[1] or not grid_valid(target):
                    continue
                other = grid_point(target)
                candidate = distance+math.dist(point, other)
                if candidate < grid_best.get(target, math.inf) and clearance(point, other, RADIUS) >= RADIUS-1e-6:
                    grid_best[target], grid_previous[target] = candidate, key
                    heapq.heappush(pending, (candidate+math.dist(other, goal), candidate, target))
        if found is None:
            raise ValueError(f"No static walking route has actor clearance: {context['portals']['scene_name']} {start} -> {goal}")
        route, current = [list(goal)], found
        while current is not None:
            route.append(grid_point(current))
            current = grid_previous[current]
        route.append(list(start))
        route.reverse()
        simplified, current = [route[0]], 0
        while current < len(route)-1:
            following = next(i for i in range(len(route)-1, current, -1)
                             if clearance(route[current], route[i], RADIUS) >= RADIUS-1e-6)
            simplified.append(route[following])
            current = following
        return {"method": "collision-derived0.4-unit grid; exact segment-clearance verification",
                "length_world_units": sum(math.dist(a, b) for a, b in zip(simplified, simplified[1:])),
                "minimum_static_clearance": min(clearance(a, b) for a, b in zip(simplified, simplified[1:])),
                "waypoints": [{"source_nav_index": None, "position_xyz": [p[0], source_floor_height(collision["floor"], *p), p[1]]}
                              for p in simplified]}
    indices, current = [], count-1
    while True:
        indices.append(current)
        if current == count-2:
            break
        current = previous[current]
    indices.reverse()
    return {"method": "original navigation-point coordinates; verified visibility edges",
            "length_world_units": best[count-1],
            "minimum_static_clearance": min(clearance(points[a], points[b]) for a, b in zip(indices, indices[1:])),
            "waypoints": [{"source_nav_index": i if i < count-2 else None,
                           "position_xyz": [points[i][0], source_floor_height(collision["floor"], *points[i]), points[i][1]]}
                          for i in indices]}


def run_case(game):
    contexts, _ = load_contexts(game)
    def marker(name, index):
        return contexts[name]["scene"]["markers"][index][::2]
    def portal(name, index):
        plate = next(p for p in contexts[name]["portals"]["portals"] if p["index"] == index)
        if plate["mode"] != 0:
            raise ValueError("Expected actual mode0 passage")
        boundary = [(a[::2], b[::2]) for a, b in floor_boundary_edges(contexts[name]["collision"]["floor"])]
        center = [sum(point[axis] for point in plate["points"])/len(plate["points"]) for axis in (0, 2)]
        if spawn_clearance(contexts[name]["collision"], *center, floor_edges=boundary) >= RADIUS:
            return center
        # A real passage can cover part of an obstacle. Find a clear interior
        # plate point; never move geometry or the original arrival marker.
        if plate["shape"] != 0:
            raise ValueError("Nonrectangular portal center is obstructed")
        a, b = plate["points"]
        candidates = [[a[0]+(b[0]-a[0])*i/32, a[2]+(b[2]-a[2])*j/32]
                      for i in range(1, 32) for j in range(1, 32)]
        result = max(candidates, key=lambda point: spawn_clearance(contexts[name]["collision"], *point, floor_edges=boundary))
        if spawn_clearance(contexts[name]["collision"], *result, floor_edges=boundary) < RADIUS:
            raise ValueError("Original portal plate has no clear interior point")
        return result
    legs = [
        ("ak09", marker("ak09", 2), portal("ak09", 11), "cabin arrival2 to AL01 passage11; source405 default0", 11),
        ("al01", marker("al01", 1), portal("al01", 6), "AK09 arrival1 to AL03 passage6", 6),
        ("al03", marker("al03", 1), CORD_PICKUP, "AL01 arrival1 to actual cord pickup trigger95", None),
        ("al03", CORD_PICKUP, portal("al03", 4), "pickup to return AL01 passage4", 4),
        ("al01", marker("al01", 2), portal("al01", 5), "AL03 arrival2 to AK09 passage5", 5),
        ("ak09", marker("ak09", 6), portal("ak09", 6), "AL01 arrival6 to AH50 passage6", 6),
        ("ah50", marker("ah50", 1), portal("ah50", 3), "AK09 arrival1 to AH01 passage3; source435 default0", 3),
        ("ah01", marker("ah01", 1), CORD_USE, "AH50 arrival1 to actual cord use trigger50/marker23", None),
        ("ah01", CORD_USE, portal("ah01", 3), "after actual use callback1945 enables passage3 to AH02", 3),
    ]
    policies = {name: SceneActivationCase(game).activate_arrival(name)["portal_enabled"]
                for name in {leg[0] for leg in legs}}
    def verified_leg(name, start, end, purpose, index):
        route = route_between(contexts[name], start, end)
        positions = [point["position_xyz"] for point in route["waypoints"]]
        unexpected = set()
        for passage in contexts[name]["portals"]["portals"]:
            if passage["mode"] != 0 or passage["index"] == index or not policies[name][passage["index"]]:
                continue
            p = passage["points"]
            if passage["shape"] == 0:
                a, b = p
                polygon = [[a[0], a[2]], [a[0], b[2]], [b[0], b[2]], [b[0], a[2]]]
            else:
                polygon = [point[::2] for point in p]
            for a, b in zip(positions, positions[1:]):
                if contains(passage, a) or contains(passage, b) or any(
                        crossing(a[::2], b[::2], c, d) for c, d in zip(polygon, polygon[1:]+polygon[:1])):
                    unexpected.add(passage["index"])
        if unexpected:
            raise ValueError(f"Walking route intersects another source-active passage: {name}/{unexpected}")
        return {"scene": name, "purpose": purpose, "portal_index": index,
                "scene_native_id": contexts[name]["id"],
                "scene_payload_sha256": hashlib.sha256(contexts[name]["payload"]).hexdigest(),
                "active_non_target_portal_intersections": [], **route}
    return {"script_sha256": hashlib.sha256((game / "data/script/pc/levelA.jpc").read_bytes()).hexdigest(),
            "actor_radius": RADIUS,
            "startup_clearance_note": {"scene": "ak01", "source_marker": 4,
                "original_xyz": contexts["ak01"]["scene"]["markers"][4],
                "clearance": spawn_clearance(contexts["ak01"]["collision"], *marker("ak01", 4)),
                "limitation": "Original startup point is outside solid25 but overlaps the modern0.25-radius inflation. An outward-escape rule is required before applying this route proof to full new-game startup; no moved spawn is fabricated."},
            "limits": "Static walking clearance only. Waypoints prefer original navigation-point coordinates with a collision-derived grid fallback. Portal goals use original plate center or a verified clear interior point. Segments are checked against original solids/circles/floor boundaries and other source-active portals. Does not claim original AI path-link policy or bypass source item/progression conditions. Dynamic NPCs are not placed or removed.",
            "legs": [verified_leg(*leg) for leg in legs]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    text = json.dumps(run_case(args.game), indent=2)
    if args.output:
        args.output.write_text(text+"\n", encoding="utf-8")
    else:
        print(text)


if __name__ == "__main__":
    main()
