"""Check the four native door transitions against original PC/JPC resources.

Trace positions are post-event world coordinates. Attempt records the old source
pose; transition records arrival and the old gate snapshot. Activate resets gates.
Fixture explicitly declares synthetic actor placement; it never resets door state.
"""

import argparse
import csv
import json
import math
from pathlib import Path

from ed_assets import parse_archive
from ed_collision import parse_collision
from ed_doors import decode_doors
from ed_portals import active_portals, parse_portals, scene_name
from ed_scripts import ScriptPackage, source_facing_allows
from ed_triggers import contains
from export_scene import ROOT, floor_boundary_edges, source_floor_height
from verify_scene_trace import inside_polygon, point_segment_distance


NUMBERS = ("source_tick", "portal_index", "enabled", "pending", "elapsed", "guard", "voice",
           "x", "y", "z", "yaw", "camera_index", "active_enabled", "active_pending")
INTEGERS = set(NUMBERS) - {"x", "y", "z", "yaw"}
EVENTS = {"fixture", "use", "ready", "attempt", "transition", "activate", "failure", "unsupported", "rejected"}


def load_sources(game):
    package = ScriptPackage((game / "data/script/pc/levelA.jpc").read_bytes())
    sources, destinations = {}, {}
    for name in ("ak01", "ak09"):
        blob = (game / f"data/{name}.pc").read_bytes()
        entries = [entry for entry in parse_archive(blob) if entry.resource_id >> 24 == 15]
        if len(entries) != 1:
            raise ValueError("Expected one original scene resource")
        entry = entries[0]
        payload = blob[entry.offset:entry.offset + entry.size]
        identity = int.from_bytes(blob[:4], "little")
        native = parse_portals(payload, identity)
        sources[name] = {"payload": payload, "native": native, "collision": parse_collision(payload)}
        destinations[identity] = native
    for source in sources.values():
        policy = decode_doors(source["payload"], source["native"]["scene_id"], package, destinations)
        source["policy"] = policy
        source["doors"] = {door["portal"]["index"]: door for door in policy["doors"]}
        source["boundary"] = [(a[::2], b[::2]) for a, b in floor_boundary_edges(source["collision"]["floor"])]
    return sources


def _scene(value):
    value = str(value).strip().lower()
    if value in ("", "-", "none"):
        return ""
    textual = (len(value) == 4 and all("a" <= c <= "z" for c in value[:2]) and value[2:].isdigit())
    return value if textual else scene_name(int(value, 0))


def _row(raw):
    row = dict(raw)
    for key in NUMBERS:
        value = float(row[key])
        if not math.isfinite(value) or (key in INTEGERS and value != int(value)):
            raise ValueError(f"Invalid numeric field {key}")
        row[key] = int(value) if key in INTEGERS else value
    for key in ("active_scene", "source_scene", "destination"):
        row[key] = _scene(row[key])
    if row["event"] not in EVENTS or any(row[key] not in (0, 1) for key in ("enabled", "pending", "voice")):
        raise ValueError("Invalid event or boolean gate field")
    if any(row[key] < 0 for key in ("source_tick", "elapsed", "guard", "active_enabled", "active_pending")):
        raise ValueError("Negative source clock or state count")
    return row


def _same_pose(a, b):
    return (max(abs(a[key] - b[key]) for key in ("x", "y", "z")) <= 0.0002
            and abs(math.remainder(a["yaw"] - b["yaw"], 2 * math.pi)) <= 0.00002
            and a["camera_index"] == b["camera_index"])


def _snapshot(state, tick):
    if not state:
        return 0, 0, 0
    elapsed = tick - state["use_tick"] if state["pending"] else state["ready"]["elapsed"]
    return int(state["enabled"]), int(state["pending"]), elapsed


def verify_rows(rows, sources):
    expected_routes = {(name, index) for name, source in sources.items() for index in source["doors"]}
    completed, states = set(), {}
    active, pending_activation, pending_attempt = None, None, None
    guard_value = guard_tick = last_tick = 0
    minimum_clearance, rollbacks, unsupported, count, attempts, transitions = math.inf, 0, 0, 0, 0, 0

    for line, raw in enumerate(rows, 2):
        try:
            row = _row(raw)
            event, tick, name = row["event"], row["source_tick"], row["active_scene"]
            if name not in sources or row["source_scene"] not in sources or tick < last_tick:
                raise ValueError("Unknown scene or source clock moved backwards")
            source = sources[name]
            point = [row["x"], row["y"], row["z"]]
            height = source_floor_height(source["collision"]["floor"], point[0], point[2])
            if abs(point[1] - height) > 0.0002 or not 0 <= row["camera_index"] < source["native"]["camera_count"]:
                raise ValueError("Actor world Y/camera does not match the original destination floor")
            xz = [point[0], point[2]]
            for solid in source["collision"]["solids"]:
                polygon = solid["vertices_xz"]
                if inside_polygon(xz, polygon):
                    raise ValueError("Actor is inside an original solid")
                minimum_clearance = min(minimum_clearance, *(point_segment_distance(xz, a, b)
                    for a, b in zip(polygon, polygon[1:] + polygon[:1])))
            minimum_clearance = min(minimum_clearance, *(point_segment_distance(xz, a, b)
                for a, b in source["boundary"]))
            if minimum_clearance < 0.25 - 0.0002:
                raise ValueError("Actor does not have the prototype 0.25 clearance")

            if pending_activation is not None and event != "activate":
                raise ValueError("A committed transition must be followed by destination activation")
            if pending_attempt is not None and event not in ("transition", "failure"):
                raise ValueError("A load attempt must be followed by commit or rollback")
            if active is None:
                if (event != "activate" or tick != 0 or row["source_scene"] != name
                        or row["portal_index"] != -1 or row["destination"] != name
                        or any(row[key] for key in ("enabled", "pending", "elapsed", "guard", "active_enabled", "active_pending"))):
                    raise ValueError("Trace must begin with the initial disabled scene activation")
                active, states = name, {index: {} for index in source["doors"]}
            elif event == "fixture":
                if name != active or row["source_scene"] != active:
                    raise ValueError("Actor fixture changed the world scene")
                state = states.get(row["portal_index"], {})
                if (row["enabled"], row["pending"], row["elapsed"]) != _snapshot(state, tick):
                    raise ValueError("Actor fixture reset or changed a gate state")
            elif event == "activate":
                if (pending_activation is None or name != active or row["source_scene"] != name
                        or row["portal_index"] != -1 or row["destination"] != name or not _same_pose(row, pending_activation)
                        or any(row[key] for key in ("enabled", "pending", "elapsed", "active_enabled", "active_pending"))):
                    raise ValueError("Destination activation did not preserve arrival pose and disable gates")
                pending_activation = None
            else:
                old_name = row["source_scene"]
                if old_name != active:
                    raise ValueError("Event source differs from the active source scene")
                old_source = sources[old_name]
                index = row["portal_index"]
                door = old_source["doors"].get(index)
                if event == "unsupported":
                    known = {p["index"]: p for p in old_source["policy"]["unsupported_portals"]}
                    if index not in known or row["enabled"] or row["pending"]:
                        raise ValueError("Unsupported portal was enabled or has no source metadata")
                    if row["destination"] != known[index]["destination_scene"]:
                        raise ValueError("Unsupported portal destination metadata changed")
                    unsupported += 1
                elif event == "rejected" and index == -1:
                    if row["destination"] != active or any(row[key] for key in ("enabled", "pending", "elapsed")):
                        raise ValueError("Rejected action without a candidate contains gate effects")
                elif door is None:
                    raise ValueError("An undecoded portal was executed")
                else:
                    state = states[index]
                    target = door["transition"]
                    timing = old_source["policy"]["timing"]
                    if row["destination"] != target["scene_name"]:
                        raise ValueError("Event destination differs from the native portal")
                    if event == "use":
                        if max(0, guard_value - (tick - guard_tick)):
                            raise ValueError("Use occurred during the source input guard")
                        delta = row["yaw"] - math.atan2(door["facing_markerXYZ"][0] - point[0],
                                                        door["facing_markerXYZ"][2] - point[2])
                        if not contains(door["interaction_zone"], point) or not source_facing_allows(delta, door["gate"]["angle_argument_degrees"]):
                            raise ValueError("Use failed original interaction-zone/facing predicates")
                        was_enabled = bool(state.get("enabled"))
                        if (row["enabled"], row["pending"], row["elapsed"], row["guard"]) != (int(was_enabled), 1, 0, timing["input_guard_ticks"]):
                            raise ValueError("Use did not create the source pending sound state")
                        state.update(use_tick=tick, enabled=was_enabled, pending=True)
                        guard_value, guard_tick = timing["input_guard_ticks"], tick
                    elif event == "ready":
                        elapsed = tick - state.get("use_tick", tick + 1)
                        if (not state or not state["pending"] or row["elapsed"] != elapsed
                                or not timing["delay_ticks"] <= elapsed <= timing["latest_enable_ticks"]
                                or (row["voice"] and elapsed < timing["latest_enable_ticks"])
                                or (row["enabled"], row["pending"]) != (1, 0)):
                            raise ValueError("Portal enable timing differs from the source sound gate")
                        state.update(enabled=True, pending=False, ready=row)
                    elif event == "attempt":
                        if not state.get("enabled") or (row["enabled"], row["pending"], row["elapsed"]) != _snapshot(state, tick):
                            raise ValueError("Transition attempted without the enabled source gate")
                        if name != old_name or not active_portals(old_source["native"], point, [index]):
                            raise ValueError("Transition did not originate inside its original portal plate")
                        pending_attempt = row
                        attempts += 1
                    elif event in ("transition", "failure"):
                        attempt = pending_attempt
                        if (attempt is None or any(row[key] != attempt[key] for key in
                                ("source_tick", "source_scene", "portal_index", "destination", "enabled", "pending", "elapsed", "guard"))):
                            raise ValueError("Commit/rollback does not match its source load attempt")
                        if event == "failure":
                            if name != old_name or not _same_pose(row, attempt):
                                raise ValueError("Failed reload changed source scene or actor pose")
                            rollbacks += 1
                        else:
                            spawn = target["spawn_xyz"]
                            expected = dict(row, x=spawn[0], z=spawn[2],
                                y=source_floor_height(source["collision"]["floor"], spawn[0], spawn[2]) + spawn[1],
                                yaw=target["yaw"], camera_index=target["camera_index"])
                            if name != target["scene_name"] or not _same_pose(row, expected):
                                raise ValueError("Native destination marker/Y/yaw/camera did not match arrival")
                            completed.add((old_name, index))
                            transitions += 1
                            active, states = name, {i: {} for i in source["doors"]}
                            pending_activation = row
                        pending_attempt = None
                    elif event == "rejected":
                        if (row["enabled"], row["pending"], row["elapsed"]) != _snapshot(state, tick):
                            raise ValueError("Rejected interaction changed the source gate")
                    else:
                        raise ValueError("Unexpected gate event")
            if event != "transition" and name != active:
                raise ValueError("Scene changed without a committed native transition")
            if row["guard"] != max(0, guard_value - (tick - guard_tick)):
                raise ValueError("Global input guard was reset or decremented incorrectly")
            if row["active_enabled"] != sum(bool(s.get("enabled")) for s in states.values()) or row["active_pending"] != sum(bool(s.get("pending")) for s in states.values()):
                raise ValueError("Active gate counts differ from source state or activated return gates")
            last_tick, count = tick, count + 1
        except (KeyError, ValueError, IndexError) as error:
            raise ValueError(f"Trace line {line}: {error}") from error
    if pending_activation is not None or pending_attempt is not None or completed != expected_routes:
        raise ValueError(f"Trace did not activate all four source transitions; verified {sorted(completed)}")
    return {"rows": count, "verified_transitions": [f"{name}:{index}" for name, index in sorted(completed)],
            "transition_events": transitions, "load_attempts": attempts,
            "verified_reload_rollbacks": rollbacks, "unsupported_events": unsupported,
            "minimum_clearance": minimum_clearance, "fixture_pose_setup_is_synthetic": True,
            "scope": "Event trace verifies source door gates/arrivals, not continuous motion or native actor FSM116."}


def verify(trace, game):
    with trace.open(newline="", encoding="utf-8") as stream:
        return verify_rows(csv.DictReader(stream), load_sources(game))


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
