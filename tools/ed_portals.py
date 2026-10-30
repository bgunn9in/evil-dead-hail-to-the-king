"""Read original native portal volumes and resolve their destination markers."""

import math
import struct

from ed_camera import parse_scene
from ed_collision import parse_collision, point_spatial_mask
from ed_triggers import contains


def scene_name(scene_id: int) -> str:
    """Decode the original two letters + decimal scene number identifier."""
    first, second, number, variant = struct.pack(">I", scene_id)
    if not (97 <= first <= 122 and 97 <= second <= 122 and number <= 99 and variant == 0):
        raise ValueError("Unsupported scene identifier")
    return f"{chr(first)}{chr(second)}{number:02d}"


def parse_portals(data: bytes, scene_id: int) -> dict:
    """Return native metadata; script enable/lock state is deliberately separate.

    Mode 1 only preloads another scene. Mode 0 performs a transition when the
    actor is inside and the separate runtime portal enable state equals 1.
    """
    name = scene_name(scene_id)
    scene = parse_scene(data)
    floor = parse_collision(data)["floor"]
    count, offset = struct.unpack_from("<II", data, 0x28)
    if offset > len(data) or count > (len(data) - offset) // 72 or (count and offset < 88):
        raise ValueError("Portal table exceeds scene bounds")
    portals = []
    for index in range(count):
        start = offset + index * 72
        mode, shape, camera, marker = struct.unpack_from("<4B", data, start)
        if mode not in (0, 1) or shape not in ((0, 2) if mode == 0 else (0, 1, 2)):
            raise ValueError("Unsupported native portal mode or shape")
        point_count = {0: 2, 1: 3, 2: 4}[shape]
        points = [list(point) for point in struct.iter_unpack(
            "<3f", data[start + 4:start + 4 + point_count * 12]
        )]
        rotation = list(struct.unpack_from("<3f", data, start + 60))
        if not all(math.isfinite(value) for point in points + [rotation] for value in point):
            raise ValueError("Non-finite portal coordinate or rotation")
        destination = struct.unpack_from("<I", data, start + 56)[0]
        portals.append({
            "index": index, "mode": mode, "kind": "transition" if mode == 0 else "preload",
            "shape": shape, "points": points,
            "bounds_xz": [min(p[0] for p in points), min(p[2] for p in points),
                          max(p[0] for p in points), max(p[2] for p in points)],
            "spatial_mask": struct.unpack_from("<I", data, start + 52)[0],
            "destination_scene_id": destination, "destination_scene": scene_name(destination),
            "camera_index": camera, "marker_index": marker,
            "rotation_xyz": rotation, "yaw": rotation[1],
            "enabled_on_native_load": True,
        })
    return {"scene_id": scene_id, "scene_name": name, "portals": portals,
            "markers": scene["markers"], "camera_count": len(scene["tracks"]),
            "spatial_grid": {"grid_origin_xz": floor["grid_origin_xz"],
                             "grid_extent_xz": floor["grid_extent_xz"], "bins": 16}}


def actor_in_portal(portal: dict, position) -> bool:
    """Match actorInPortal: geometry only, independent of enable and preload mode."""
    return contains(portal, position)


def active_portals(data: dict, position, enabled_indices) -> list[dict]:
    """Native source-order hits after spatial gating and explicit script state."""
    enabled = set(enabled_indices)
    mask = point_spatial_mask({"floor": data["spatial_grid"]}, position[0], position[2])
    return [portal for portal in data["portals"]
            if portal["index"] in enabled
            and (mask & portal["spatial_mask"] & 0xFFFF)
            and (mask & portal["spatial_mask"] & 0xFFFF0000)
            and actor_in_portal(portal, position)]


def resolve_transition(portal: dict, destination: dict) -> dict:
    """Resolve source marker X/Z, native initial Y=0, and rotation in radians.

    The original calls actorSetPositionMarker, which discards marker Y, then
    resumes physics on the destination floor. Retain marker_xyz for diagnostics.
    """
    if portal["mode"] != 0:
        raise ValueError("A preload portal does not move the player")
    if destination["scene_id"] != portal["destination_scene_id"]:
        raise ValueError("Portal destination scene does not match")
    marker = portal["marker_index"]
    if marker >= len(destination["markers"]):
        raise ValueError("Portal destination marker is missing")
    camera = portal["camera_index"]
    if camera >= destination["camera_count"]:
        raise ValueError("Portal destination camera is missing")
    position = destination["markers"][marker]
    return {"scene_id": destination["scene_id"], "scene_name": destination["scene_name"],
            "camera_index": camera, "marker_index": marker,
            "marker_xyz": list(position), "spawn_xyz": [position[0], 0.0, position[2]],
            "rotation_xyz": list(portal["rotation_xyz"]), "yaw": portal["yaw"]}
