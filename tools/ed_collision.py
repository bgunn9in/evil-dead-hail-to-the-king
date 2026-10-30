"""Read the original PC floor mesh and static XZ collision volumes."""

import math
import struct


def _finite(values):
    if any(not math.isfinite(value) for value in values):
        raise ValueError("Non-finite collision coordinate")


def _polygon(data, offset, volume_index):
    kind, shape, enabled_raw, raw_byte3 = struct.unpack_from("<4B", data, offset)
    if shape not in (0, 1, 2, 3):
        raise ValueError(f"Volume {volume_index}: unsupported static shape {shape}")
    points = [list(point) for point in struct.iter_unpack(
        "<3f", data[offset + 4:offset + 4 + (2 if shape in (0, 3) else shape + 2) * 12]
    )]
    _finite(value for point in points for value in point)
    if shape == 3:
        radius = math.sqrt(sum(value * value for value in points[1])) or 1.0
        return {"volume_index": volume_index, "type": kind, "shape": shape, "vertices_xz": [],
                "circle_xz": {"center": [points[0][0], points[0][2]], "radius": radius},
                "source_vertices_xyz": points, "enabled_raw": enabled_raw, "raw_byte3": raw_byte3,
                "spatial_mask_raw": struct.unpack_from("<I", data, offset + 0x3C)[0],
                "flags_raw": struct.unpack_from("<I", data, offset + 0x40)[0]}
    if shape == 0:
        x0, x1 = sorted(point[0] for point in points)
        z0, z1 = sorted(point[2] for point in points)
        polygon = [[x0, z0], [x0, z1], [x1, z1], [x1, z0]]
    else:
        polygon = [[point[0], point[2]] for point in points]
    count = len(polygon)
    area2 = sum(point[0] * polygon[(index + 1) % count][1]
                - polygon[(index + 1) % count][0] * point[1]
                for index, point in enumerate(polygon))
    if abs(area2) <= 1e-10:
        raise ValueError(f"Volume {volume_index}: degenerate static polygon")
    if area2 > 0:
        polygon.reverse()
    for index in range(count):
        a, b, c = (polygon[(index + delta) % count] for delta in range(3))
        if (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]) > 1e-6:
            raise ValueError(f"Volume {volume_index}: non-convex static polygon")
    return {
        "volume_index": volume_index,
        "type": kind,
        "shape": shape,
        "vertices_xz": polygon,
        "source_vertices_xyz": points,
        "enabled_raw": enabled_raw,
        "raw_byte3": raw_byte3,
        "spatial_mask_raw": struct.unpack_from("<I", data, offset + 0x3C)[0],
        "flags_raw": struct.unpack_from("<I", data, offset + 0x40)[0],
    }


def _point_only(data, offset, volume_index):
    """419F50 returns0 for shapes4/5: retain point metadata without a wall."""
    kind, shape, enabled_raw, raw_byte3 = struct.unpack_from("<4B", data, offset)
    points = [list(p) for p in struct.iter_unpack("<3f", data[offset + 4:offset + 28])]
    _finite(value for point in points for value in point)
    if shape == 5 and any(a > b for a, b in zip(points[0], points[1])):
        raise ValueError(f"Volume {volume_index}: invalid raw XYZbox bounds")
    return {"volume_index": volume_index, "type": kind, "shape": shape,
            "source_vertices_xyz": points, "enabled_raw": enabled_raw, "raw_byte3": raw_byte3,
            "spatial_mask_raw": struct.unpack_from("<I", data, offset + 60)[0],
            "flags_raw": struct.unpack_from("<I", data, offset + 64)[0],
            "point_only": True, "source_segment_collision": False}


def parse_collision(data: bytes) -> dict:
    """Return uninflated source geometry; type-1 regions are not blocking walls.

    Types 0/2 use XZ point tests in the original engine, irrespective of their
    stored Y values. Script-driven activation and dynamic objects are separate.
    """
    if len(data) < 88:
        raise ValueError("Truncated scene header")
    floor_size, floor_offset, volume_count, volume_offset = struct.unpack_from("<4I", data)
    if floor_offset < 88 or floor_offset > len(data) or floor_size > len(data) - floor_offset:
        raise ValueError("Floor section exceeds scene bounds")
    if volume_offset < 88 or volume_offset > len(data) or volume_count > (len(data) - volume_offset) // 72:
        raise ValueError("Volume table exceeds scene bounds")
    if floor_offset + floor_size > volume_offset:
        raise ValueError("Floor section overlaps the volume table")
    floor_data = data[floor_offset:floor_offset + floor_size]
    if len(floor_data) < 64:
        raise ValueError("Truncated floor header")
    header = struct.unpack_from("<9f", floor_data)
    _finite(header)
    if any(header[axis] > header[axis + 3] for axis in range(3)) or header[7] <= 0 or header[8] <= 0:
        raise ValueError("Invalid floor bounds or spatial grid extent")
    vertices_count, normals_count, faces_count = struct.unpack_from("<3I", floor_data, 36)
    vertices_end = 64 + vertices_count * 12
    normals_end = vertices_end + normals_count * 12
    faces_end = normals_end + faces_count * 8
    flags_end = faces_end + faces_count * 4
    if flags_end > len(floor_data):
        raise ValueError("Floor arrays exceed the floor section")
    vertices = [list(point) for point in struct.iter_unpack("<3f", floor_data[64:vertices_end])]
    normals = [list(point) for point in struct.iter_unpack("<3f", floor_data[vertices_end:normals_end])]
    _finite(value for point in vertices + normals for value in point)
    face_records = list(struct.iter_unpack("<4H", floor_data[normals_end:faces_end]))
    if any(index >= vertices_count for face in face_records for index in face[:3]):
        raise ValueError("Floor triangle index is outside the vertex array")
    floor = {
        "vertices": vertices,
        "triangles": [list(face[:3]) for face in face_records],
        "triangle_padding_raw": [face[3] for face in face_records],
        "normals_raw": normals,
        "surface_flags_raw": list(struct.unpack_from(f"<{faces_count}I", floor_data, faces_end)),
        "bounds_min": list(header[:3]),
        "bounds_max": list(header[3:6]),
        "grid_origin_xz": [header[0], header[2]],
        "grid_extent_xz": [header[7], header[8]],
        "undecoded_tail_bytes": len(floor_data) - flags_end,
    }
    solids, regions, point_only_solids, point_only_regions = [], [], [], []
    for index in range(volume_count):
        offset = volume_offset + index * 72
        kind = data[offset]
        if kind == 2:
            (point_only_solids if data[offset + 1] in (4, 5) else solids).append(
                _point_only(data, offset, index) if data[offset + 1] in (4, 5) else _polygon(data, offset, index))
        elif kind == 1:
            (point_only_regions if data[offset + 1] in (4, 5) else regions).append(
                _point_only(data, offset, index) if data[offset + 1] in (4, 5) else _polygon(data, offset, index))
    navigation = {"points": [], "links": []}
    for field, header_offset, stride in (("points", 0x30, 44), ("links", 0x38, 32)):
        count, offset = struct.unpack_from("<2I", data, header_offset)
        if count and (offset < 88 or offset > len(data) or count > (len(data) - offset) // stride):
            raise ValueError(f"Navigation {field} exceed scene bounds")
        if field == "points":
            navigation[field] = [list(struct.unpack_from("<3f", data, offset + index * stride + 8))
                                 for index in range(count)]
            _finite(value for point in navigation[field] for value in point)
        else:
            navigation[field] = [list(struct.unpack_from("<2I", data, offset + index * stride + 24))
                                 for index in range(count)]
            if any(index >= len(navigation["points"]) for pair in navigation[field] for index in pair):
                raise ValueError("Navigation link references an invalid node")
    return {"floor": floor, "solids": solids, "regions": regions, "navigation": navigation,
            "point_only_solids": point_only_solids, "point_only_regions": point_only_regions}


def point_spatial_mask(collision: dict, x: float, z: float) -> int:
    """Reproduce 0x0041CEB0, including x86 shift wrapping outside the grid."""
    _finite((x, z))
    floor = collision["floor"]
    ix, iz = [int((value - origin) / extent * 16)
              for value, origin, extent in zip((x, z), floor["grid_origin_xz"], floor["grid_extent_xz"])]
    return (1 << (ix & 31)) | (1 << ((iz + 16) & 31))
