"""Join the four verified AK01/AK09 native portals and JPC Use policies."""

import argparse
import json
import math
from pathlib import Path
import struct

from ed_assets import parse_archive
from ed_portals import parse_portals, resolve_transition
from ed_scripts import ScriptPackage, extract_portal_policy, extract_portal_timing


AK01, AK09 = 0x616B0100, 0x616B0900
# shortcut: reject unresearched doors until their script conditions are decoded.
# portal: (interaction zone, facing marker, angle, destination, camera, arrival marker)
_SCENES = {
    AK01: ("SceneAK01", {3: (69, 28, 40, AK09, 12, 3), 2: (74, 32, 45, AK09, 8, 2)}),
    AK09: ("SceneAK09", {8: (57, 51, 40, AK01, 11, 1), 7: (58, 52, 40, AK01, 3, 0)}),
}


def _interaction_zone(data, index, shapes=(0, 2)):
    count, offset = struct.unpack_from("<II", data, 8)
    if not 0 <= index < count:
        raise ValueError("Door interaction zone is missing")
    start = offset + index * 72
    kind, shape, enabled, byte3 = struct.unpack_from("<4B", data, start)
    if kind != 6 or shape not in shapes:
        raise ValueError("Unsupported door interaction volume type or shape")
    points = [list(point) for point in struct.iter_unpack(
        "<3f", data[start + 4:start + 4 + (2 if shape == 0 else shape + 2) * 12]
    )]
    if not all(math.isfinite(value) for point in points for value in point):
        raise ValueError("Non-finite door interaction coordinate")
    bounds = [min(p[0] for p in points), min(p[2] for p in points),
              max(p[0] for p in points), max(p[2] for p in points)]
    if bounds[0] >= bounds[2] or bounds[1] >= bounds[3]:
        raise ValueError("Degenerate door interaction volume")
    if shape:
        for i in range(len(points)):
            a, b, c = (points[(i + j) % len(points)] for j in range(3))
            if (b[0] - a[0]) * (c[2] - b[2]) - (b[2] - a[2]) * (c[0] - b[0]) >= 0:
                raise ValueError("Door interaction polygon is not strictly clockwise and convex")
    return {"index": index, "type": kind, "shape": shape, "points": points,
            "bounds_xz": bounds, "spatial_mask": struct.unpack_from("<I", data, start + 60)[0],
            "raw_enabled_byte": enabled, "raw_byte3": byte3}


def _overlaps(a, b):
    def polygon(zone):
        x0, z0, x1, z1 = zone["bounds_xz"]
        return ([[x0, z0], [x0, z1], [x1, z1], [x1, z0]] if zone["shape"] == 0
                else [[p[0], p[2]] for p in zone["points"]])

    pa, pb = polygon(a), polygon(b)
    for poly in (pa, pb):
        for p, q in zip(poly, poly[1:] + poly[:1]):
            axis = [p[1] - q[1], q[0] - p[0]]
            aa, bb = [[sum(x * y for x, y in zip(point, axis)) for point in points]
                      for points in (pa, pb)]
            if max(aa) < min(bb) or max(bb) < min(aa):
                return False
    return True


def _validate_context(data, package, policy, doors):
    """Check the successful selected path; disjoint other triggers are false."""
    instructions = package.disassemble(policy["allow_use_method"])
    indices = {item["offset"]: index for index, item in enumerate(instructions)}
    trigger_calls = {}
    for index, item in enumerate(instructions):
        method = item.get("method", {})
        if (method.get("class_name"), method.get("name")) != ("ed/base/EDActor", "inTrigger"):
            continue
        if not index or instructions[index - 1]["opcode"] not in (0x10, 0x11):
            raise ValueError("Unsupported interaction zone argument")
        trigger_calls[index] = instructions[index - 1]["operands"][0]
    # Shape1 is an unrelated triangular trigger in AK01, checked only for separation.
    zones = {zone: _interaction_zone(data, zone, (0, 1, 2)) for zone in set(trigger_calls.values())}
    for door in doors:
        gate, zone = door["gate"], door["interaction_zone"]
        if any(_overlaps(zone, other) for index, other in zones.items() if index != zone["index"]):
            raise ValueError("Another allowUse interaction overlaps the selected door")
        helper = indices[gate["source_offsets"]["pending_portal"]]
        current, seen, called = 0, set(), False
        while current not in seen and current < len(instructions):
            seen.add(current)
            item = instructions[current]
            opcode = item["opcode"]
            if opcode not in (*range(0x02, 0x09), 0x10, 0x11, 0x12, 0x13, 0x1C, 0x2B,
                              0x3D, 0x99, 0xA7, 0xAC, 0xB6, 0xB8):
                raise ValueError("Additional instruction on selected door path")
            if opcode == 0x99:
                if current - 1 in trigger_calls:
                    succeeds = trigger_calls[current - 1] == zone["index"]
                elif (current and instructions[current - 1]["offset"]
                      == gate["source_offsets"]["facing_marker"]):
                    succeeds = True
                else:
                    raise ValueError("Additional condition on selected door path")
                current = current + 1 if succeeds else indices[item["target"]]
                continue
            if opcode == 0xA7:
                current = indices[item["target"]]
                continue
            if "target" in item or "cases" in item:
                raise ValueError("Additional branch on selected door path")
            if 0xB6 <= opcode <= 0xB9 and current not in trigger_calls:
                if current == helper:
                    called = True
                elif item["offset"] != gate["source_offsets"]["facing_marker"]:
                    raise ValueError("Additional call on selected door path")
            if opcode == 0x3D and current != helper - 3:
                raise ValueError("Selected door action is overwritten")
            if opcode == 0xAC:
                if not called:
                    raise ValueError("Selected door path returns before its helper")
                break
            current += 1
        else:
            raise ValueError("Selected door path does not reach its return")


def decode_doors(scene_payload: bytes, scene_id: int, package: ScriptPackage,
                 parsed_destinations: dict[int, dict]) -> dict:
    """Return source geometry and policies, with no invented locks or door states.

    Gate order is the original allowUse branch order. Transition spawn_xyz keeps
    actor-local Y=0; rendering adds destination BV height to that local height.
    Other interaction branches must have disjoint geometry; their effects are
    outside this bounded reader. Selected paths reject additional conditions.
    """
    if scene_id not in _SCENES:
        raise ValueError("Door decoder only supports AK01 and AK09")
    scene_class, selected = _SCENES[scene_id]
    native = parse_portals(scene_payload, scene_id)
    policy = extract_portal_policy(package, scene_class, selected)
    doors = []
    for gate in policy["gates"]:
        index = gate["portal_index"]
        if index >= len(native["portals"]):
            raise ValueError("Selected native door portal is missing")
        portal = native["portals"][index]
        actual = (gate["trigger_index"], gate["marker_index"], gate["angle_argument_degrees"],
                  portal["destination_scene_id"], portal["camera_index"], portal["marker_index"])
        if portal["mode"] != 0 or actual != selected[index] or gate["action_assigned"] != 116:
            raise ValueError("Selected door no longer matches the verified AK01/AK09 cross-pair")
        marker = gate["marker_index"]
        if not 0 <= marker < len(native["markers"]):
            raise ValueError("Door facing marker is missing")
        destination_id = portal["destination_scene_id"]
        if destination_id not in parsed_destinations:
            raise ValueError("Parsed door destination scene is missing")
        doors.append({"portal": portal, "gate": gate,
                      "interaction_zone": _interaction_zone(scene_payload, gate["trigger_index"]),
                      "facing_markerXYZ": list(native["markers"][marker]),
                      "transition": resolve_transition(portal, parsed_destinations[destination_id])})
    _validate_context(scene_payload, package, policy, doors)
    return {"scene_name": native["scene_name"], "initially_disabled": policy["initially_disabled"],
            "timing": extract_portal_timing(package), "doors": doors,
            "unsupported_portals": [portal for portal in native["portals"]
                                    if portal["mode"] == 0 and portal["index"] not in selected]}


def main():
    parser = argparse.ArgumentParser(description="Validate the four original AK01/AK09 door joins.")
    parser.add_argument("--game", type=Path, default=Path(__file__).resolve().parents[1] / "game")
    args = parser.parse_args()
    try:
        package = ScriptPackage((args.game / "data/script/pc/levelA.jpc").read_bytes())
        payloads, destinations = {}, {}
        for scene_id, name in ((AK01, "ak01"), (AK09, "ak09")):
            data = (args.game / f"data/{name}.pc").read_bytes()
            entries = [entry for entry in parse_archive(data) if entry.resource_id >> 24 == 15]
            if len(entries) != 1:
                raise ValueError("Expected one native scene entry")
            entry = entries[0]
            payloads[scene_id] = data[entry.offset:entry.offset + entry.size]
            destinations[scene_id] = parse_portals(payloads[scene_id], scene_id)
        summary = {}
        for scene_id, payload in payloads.items():
            result = decode_doors(payload, scene_id, package, destinations)
            assert len(result["doors"]) == 2
            summary[result["scene_name"]] = {
                "initially_disabled": result["initially_disabled"],
                "doors": [{"portal": door["portal"]["index"],
                           "zone": door["interaction_zone"]["index"],
                           "destination": door["transition"]["scene_name"],
                           "camera": door["transition"]["camera_index"],
                           "arrival_marker": door["transition"]["marker_index"]}
                          for door in result["doors"]],
                "unsupported_portals": [portal["index"] for portal in result["unsupported_portals"]],
            }
        print(json.dumps(summary, indent=2))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
