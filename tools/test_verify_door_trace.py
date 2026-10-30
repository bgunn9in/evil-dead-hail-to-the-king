import copy
import math
import unittest

from verify_door_trace import verify_rows


def sources():
    result = {}
    for name, height, indices, destination in (("ak01", 0.125, (2, 3), "ak09"),
                                              ("ak09", -0.25, (7, 8), "ak01")):
        points = [[-10, height, -10], [-10, height, 10], [10, height, 10], [10, height, -10]]
        portals, doors = [], {}
        for index in indices:
            region = {"index": index, "shape": 0, "points": [[-1, 0, -1], [1, 0, 1]],
                      "spatial_mask": 0xFFFFFFFF, "mode": 0, "destination_scene": destination}
            portals.append(region)
            doors[index] = {"portal": region, "interaction_zone": region, "facing_markerXYZ": [0, 0, 5],
                            "gate": {"angle_argument_degrees": 40},
                            "transition": {"scene_name": destination, "spawn_xyz": [0.25, 0, 0.25],
                                           "yaw": math.pi, "camera_index": 3}}
        unknown = {**portals[0], "index": 9, "destination_scene": "ak10"}
        result[name] = {"native": {"camera_count": 16, "portals": portals + [unknown],
                                  "spatial_grid": {"grid_origin_xz": [-10, -10], "grid_extent_xz": [20, 20]}},
                        "collision": {"floor": {"vertices": points, "triangles": [[0, 1, 2], [0, 2, 3]]}, "solids": []},
                        "boundary": [(a[::2], b[::2]) for a, b in zip(points, points[1:] + points[:1])],
                        "doors": doors, "policy": {"unsupported_portals": [unknown],
                            "timing": {"delay_ticks": 30, "latest_enable_ticks": 45, "input_guard_ticks": 45}}}
    return result


def trace():
    data, rows, tick = sources(), [], 0
    for name, index in (("ak01", 2), ("ak09", 7), ("ak01", 3), ("ak09", 8)):
        door = data[name]["doors"][index]
        height = data[name]["collision"]["floor"]["vertices"][0][1]
        row = {"event": "fixture", "source_tick": tick, "active_scene": name, "source_scene": name,
               "portal_index": index, "destination": door["transition"]["scene_name"],
               "enabled": 0, "pending": 0, "elapsed": 0, "guard": 0, "voice": 0,
               "x": 0, "y": height, "z": 0, "yaw": 0, "camera_index": 0,
               "active_enabled": 0, "active_pending": 0}
        if not rows:
            rows.append(dict(row, event="activate", portal_index=-1, destination=name))
        rows.append(row)
        rows.append(dict(row, event="use", pending=1, guard=45, active_pending=1))
        ready = dict(row, event="ready", source_tick=tick + 30, enabled=1, elapsed=30, guard=15,
                     active_enabled=1)
        rows.extend([ready, dict(ready, event="attempt"), dict(ready, event="failure"),
                     dict(ready, event="attempt")])
        destination = door["transition"]["scene_name"]
        arrival = dict(ready, event="transition", active_scene=destination, x=0.25, z=0.25,
                       y=data[destination]["collision"]["floor"]["vertices"][0][1],
                       yaw=math.pi, camera_index=3, active_enabled=0)
        rows.extend([arrival, dict(arrival, event="activate", source_scene=destination, portal_index=-1,
                                  destination=destination, enabled=0, elapsed=0)])
        tick += 46
    baseline = dict(rows[0], event="fixture", source_tick=tick, portal_index=9, destination="ak10")
    rows.extend([baseline, dict(baseline, event="unsupported")])
    return rows


class DoorTraceTests(unittest.TestCase):
    def test_four_original_shaped_transitions_and_failed_reload_rollback(self):
        rows = trace()
        failure = next(i for i, row in enumerate(rows) if row["event"] == "failure")
        rows.insert(failure + 1, dict(rows[failure], event="fixture"))
        result = verify_rows(rows, sources())
        self.assertEqual(result["verified_transitions"], ["ak01:2", "ak01:3", "ak09:7", "ak09:8"])
        self.assertEqual(result["verified_reload_rollbacks"], 4)
        self.assertEqual(result["unsupported_events"], 1)

    def test_rejects_early_enable_wrong_arrival_and_return_gate_bounce(self):
        for event, changes in (("ready", {"source_tick": 29, "elapsed": 29, "guard": 16}),
                               ("transition", {"y": -0.5}), ("activate", {"active_enabled": 1}),
                               ("use", {"yaw": math.pi}), ("attempt", {"x": 2.0}),
                               ("failure", {"x": 0.5})):
            rows = copy.deepcopy(trace())
            row = next(row for row in rows if row["event"] == event and (event != "activate" or row["source_tick"] > 0))
            row.update(changes)
            with self.subTest(event=event), self.assertRaises(ValueError):
                verify_rows(rows, sources())

    def test_forced_enable_at_45_ticks_while_voice_still_playing(self):
        rows = trace()
        start = next(i for i, row in enumerate(rows) if row["event"] == "ready")
        end = next(i for i, row in enumerate(rows[start:], start) if row["event"] == "activate") + 1
        for row in rows[start:end]:
            row.update(source_tick=45, elapsed=45 if row["event"] != "activate" else 0, guard=0, voice=1)
        for row in rows[end:]:
            row["source_tick"] += 15
        self.assertEqual(len(verify_rows(rows, sources())["verified_transitions"]), 4)

    def test_rejected_action_without_candidate_keeps_gate_state(self):
        rows = trace()
        rows.insert(1, dict(rows[0], event="rejected", portal_index=-1, destination="ak01"))
        self.assertEqual(len(verify_rows(rows, sources())["verified_transitions"]), 4)


if __name__ == "__main__":
    unittest.main()
