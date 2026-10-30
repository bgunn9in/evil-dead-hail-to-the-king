"""Read original on-enter camera triggers and reproduce their membership rules."""

import math
import struct

from ed_collision import parse_collision, point_spatial_mask


def _table(data, header_offset, stride):
    count, offset = struct.unpack_from("<II", data, header_offset)
    if offset > len(data) or count > (len(data) - offset) // stride or (count and offset < 88):
        raise ValueError("Trigger section exceeds scene bounds")
    return count, offset


def _slice(count, first, total):
    if first > total or count > total - first:
        raise ValueError("Trigger rule reference exceeds its table")
    return range(first, first + count)


def parse_triggers(data: bytes, *, camera_events=False) -> dict:
    """Return source-order camera zones, rules, actions, and their spatial grid.

    The player enter/exit conditions used by ak01/ak09 camera rules execute.
    camera_events opts into the verified AT01 enter/exit AND camera-event form.
    Callers exporting only flat enter/exit actions must leave this disabled.
    Other conditions on a camera rule fail explicitly instead of being guessed.
    Non-camera actions are preserved, but this module does not execute them.
    """
    collision = parse_collision(data)
    volume_count, volume_offset = _table(data, 8, 72)
    rule_count, rule_offset = _table(data, 0x10, 16)
    action_count, action_offset = _table(data, 0x18, 8)
    condition_count, condition_offset = _table(data, 0x20, 12)
    camera_count = struct.unpack_from("<I", data, 0x40)[0]
    zones, triggers = [], []
    for index in range(volume_count):
        offset = volume_offset + index * 72
        if data[offset] != 0:
            continue
        count, first = struct.unpack_from("<II", data, offset + 52)
        rules, on_enter_actions, on_exit_actions, conditional_rules = [], [], [], []
        for rule_index in _slice(count, first, rule_count):
            na, ia, nc, ic = struct.unpack_from("<4I", data, rule_offset + rule_index * 16)
            actions = []
            for ai in _slice(na, ia, action_count):
                packed, parameter = struct.unpack_from("<II", data, action_offset + ai * 8)
                action = {"index": ai, "type": packed & 255, "subtype": (packed >> 8) & 255,
                          "flags_raw": packed >> 16, "parameter": parameter}
                if action["type"] == 0 and action["subtype"] == 0:
                    if parameter >= camera_count:
                        raise ValueError("Camera action refers to a missing track")
                    action["camera_index"] = parameter
                actions.append(action)
            conditions = []
            for ci in _slice(nc, ic, condition_count):
                kind, event_flags, actor_flags, chain, selector, parameter = struct.unpack_from(
                    "<4BII", data, condition_offset + ci * 12
                )
                conditions.append({"index": ci, "type": kind, "event_flags": event_flags,
                                   "actor_flags": actor_flags, "chain": chain,
                                   "selector_raw": selector, "parameter_raw": parameter})
            rule = {"index": rule_index, "conditions": conditions, "actions": actions}
            rules.append(rule)
            if any("camera_index" in action for action in actions):
                if (not conditions or conditions[0]["type"] != 7
                        or conditions[0]["event_flags"] not in (1, 2)
                        or conditions[0]["actor_flags"] != 1):
                    raise ValueError("Unsupported camera trigger condition")
                #408940 only reads an operator when the current condition has
                # a successor; dangling last operators are ignored (BA01/BM01).
                if any(condition["chain"] != 0 for condition in conditions[:-1]):
                    if (not camera_events or len(conditions) != 2 or conditions[0]["chain"] != 1
                            or conditions[1]["type"] != 7 or conditions[1]["event_flags"] != 8
                            or conditions[1]["actor_flags"] != 7 or conditions[1]["selector_raw"] != 0x100):
                        raise ValueError("Unsupported camera condition combination")
                    #408AF0 subtype1 matches event10. 408C25/27 rejects payload0
                    #before comparing it with the authored parameter, including0.
                    rule["camera_event"] = {"value": conditions[1]["parameter_raw"],
                                            "requires_nonzero_payload": True}
                    conditional_rules.append(rule)
                # Native408940 starts with the first result; chain0 records do
                # not alter it. The optional AND form retains its second result.
                rule["effective_condition_index"] = conditions[0]["index"]
                rule["event"] = "enter" if conditions[0]["event_flags"] == 1 else "exit"
                if "camera_event" not in rule:
                    (on_enter_actions if rule["event"] == "enter" else on_exit_actions).extend(actions)
        camera_actions = [action["camera_index"] for action in on_enter_actions if "camera_index" in action]
        exit_camera_actions = [action["camera_index"] for action in on_exit_actions if "camera_index" in action]
        if not camera_actions and not exit_camera_actions and not conditional_rules:
            continue
        shape = data[offset + 1]
        if shape not in (0, 1, 2):
            raise ValueError("Unsupported camera trigger shape")
        point_count = 2 if shape == 0 else shape + 2
        points = [list(point) for point in struct.iter_unpack(
            "<3f", data[offset + 4:offset + 4 + point_count * 12]
        )]
        if not all(math.isfinite(value) for point in points for value in point):
            raise ValueError("Non-finite camera trigger coordinate")
        if shape in (1, 2):
            for i in range(point_count):
                a, b, c = (points[(i + j) % point_count] for j in range(3))
                if (b[0] - a[0]) * (c[2] - b[2]) - (b[2] - a[2]) * (c[0] - b[0]) >= 0:
                    raise ValueError("Camera polygon is not strictly clockwise and convex")
        zones.append({"index": index, "shape": shape, "points": points,
                      "bounds_xz": [min(p[0] for p in points), min(p[2] for p in points),
                                    max(p[0] for p in points), max(p[2] for p in points)],
                      "spatial_mask": struct.unpack_from("<I", data, offset + 60)[0],
                      "enabled": True, "raw_enabled_byte": data[offset + 2]})
        triggers.append({"zone_index": index, "rules": rules,
                         "on_enter_actions": on_enter_actions, "camera_actions": camera_actions,
                         "on_exit_actions": on_exit_actions, "exit_camera_actions": exit_camera_actions,
                         "conditional_rules": conditional_rules})
    floor = collision["floor"]
    return {"zones": zones, "triggers": triggers, "camera_count": camera_count,
            "requires_camera_events": any(trigger["conditional_rules"] for trigger in triggers),
            "spatial_grid": {"grid_origin_xz": floor["grid_origin_xz"],
                             "grid_extent_xz": floor["grid_extent_xz"], "bins": 16}}


def camera_rule_actions(trigger: dict, event: str, current_camera) -> list[int]:
    """Evaluate supported original rules for one zone's current-frame event.

    416358 clears all condition results before geometry. 416EB0 supplies the
    current camera to this zone's secondary conditions before executing any of
    its actions, so all rules in this zone see the same camera payload.
    """
    result = []
    for rule in trigger["rules"]:
        if rule.get("event") != event:
            continue
        condition = rule.get("camera_event")
        if condition and (not current_camera or current_camera != condition["value"]):
            continue
        result.extend(action["camera_index"] for action in rule["actions"] if "camera_index" in action)
    return result


def contains(zone: dict, position) -> bool:
    """Native4199A0 point predicate: XZ polygons, XYZ spheres and boxes."""
    x, y, z = position
    if not math.isfinite(x) or not math.isfinite(z):
        raise ValueError("Non-finite player position")
    points = zone["points"]
    if zone["shape"] == 0:
        return (min(p[0] for p in points) <= x <= max(p[0] for p in points)
                and min(p[2] for p in points) <= z <= max(p[2] for p in points))
    if zone["shape"] in (1, 2):
        return all((b[0] - a[0]) * (z - a[2]) - (b[2] - a[2]) * (x - a[0]) < 0
                   for a, b in zip(points, points[1:] + points[:1]))
    if zone["shape"] in (3, 4):
        if not math.isfinite(y):
            raise ValueError("Non-finite player position")
        f32 = lambda value: struct.unpack("<f", struct.pack("<f", value))[0]
        delta = [f32(a - b) for a, b in zip(position, points[0])]
        squared = f32((delta[0] * delta[0] + delta[1] * delta[1]) + delta[2] * delta[2])
        radius = points[1]
        return (radius[0] * radius[0] + radius[1] * radius[1]) + radius[2] * radius[2] >= squared
    if zone["shape"] == 5:
        if not math.isfinite(y):
            raise ValueError("Non-finite player position")
        return all(low <= value <= high for value, low, high in zip(position, points[0], points[1]))
    raise ValueError("Unsupported camera trigger shape")


def update_membership(data: dict, position, inside=(), current_camera=None) -> dict:
    """Advance one original trigger update; pending actions execute in source order.

    Spatial gating deliberately preserves membership for distant skipped zones,
    matching the original engine even when a position is teleported.
    """
    membership = set(inside)
    entered = []
    exited = []
    mask = point_spatial_mask({"floor": data["spatial_grid"]}, position[0], position[2])
    for zone in data["zones"]:
        overlap = mask & zone["spatial_mask"]
        if not zone["enabled"] or not overlap & 0xFFFF or not overlap & 0xFFFF0000:
            continue
        index = zone["index"]
        if contains(zone, position):
            if index not in membership:
                entered.append(index)
                if len(membership) < 16:
                    membership.add(index)
        else:
            if index in membership:
                exited.append(index)
            membership.discard(index)
    camera_actions = []
    for trigger in data["triggers"]:
        if trigger["zone_index"] in entered:
            actions = camera_rule_actions(trigger, "enter", current_camera)
        elif trigger["zone_index"] in exited:
            actions = camera_rule_actions(trigger, "exit", current_camera)
        else:
            continue
        camera_actions.extend(actions)
        if actions:
            current_camera = actions[-1]
    return {"inside": sorted(membership), "camera": current_camera,
            "entered": entered, "exited": exited, "camera_actions": camera_actions}


def expected_camera(data: dict, position, current_camera=None, inside=()):
    """Camera after one update; an empty inside-set represents fresh scene entry."""
    return update_membership(data, position, inside, current_camera)["camera"]
