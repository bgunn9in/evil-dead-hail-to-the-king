"""Read original PC scene camera tracks and reproduce their D3D8 matrices."""

import math
import struct


def _table(data, count_offset, stride):
    count, offset = struct.unpack_from("<II", data, count_offset)
    if offset > len(data) or count > (len(data) - offset) // stride:
        raise ValueError("Scene table exceeds resource bounds")
    if count and offset < 88:
        raise ValueError("Scene table overlaps its header")
    return count, offset


def parse_scene(data: bytes) -> dict:
    """Parse camera tracks, marker positions, and navigation node positions.

    Positions use the original X/Z floor, Y-up coordinates. Marker roles and
    navigation links are not decoded; a marker is not automatically a spawn.
    """
    if len(data) < 88:
        raise ValueError("Truncated scene header")
    marker_count, marker_offset = _table(data, 0x48, 12)
    nav_count, nav_offset = _table(data, 0x30, 44)
    camera_count, camera_offset = _table(data, 0x40, 92)
    if camera_offset > marker_offset:
        raise ValueError("Camera section follows marker section")
    tracks = []
    offset = camera_offset
    for index in range(camera_count):
        if offset + 92 > marker_offset:
            raise ValueError("Truncated camera header")
        frame_count = struct.unpack_from("<I", data, offset + 0x54)[0]
        frame_offset = offset + 0x5C
        if not frame_count or frame_count > (marker_offset - frame_offset) // 28:
            raise ValueError("Invalid camera frame count")
        background_start = data[offset]
        frames = []
        for frame_index in range(frame_count):
            values = struct.unpack_from("<7f", data, frame_offset + frame_index * 28)
            if not all(math.isfinite(value) for value in values):
                raise ValueError("Non-finite camera frame")
            if not 0 < values[6] < math.pi:
                raise ValueError("Invalid camera field of view")
            frames.append({
                "index": frame_index,
                "background_index": background_start + frame_index,
                "eye": list(values[:3]),
                "rotation": list(values[3:6]),
                "fov": values[6],
            })
        tracks.append({
            "index": index,
            "offset": offset,
            "background_start": background_start,
            "frames": frames,
        })
        offset = frame_offset + frame_count * 28
    if offset != marker_offset:
        raise ValueError("Unparsed bytes after camera tracks")
    markers = [list(struct.unpack_from("<3f", data, marker_offset + i * 12))
               for i in range(marker_count)]
    navpoints = [list(struct.unpack_from("<3f", data, nav_offset + i * 44 + 8))
                 for i in range(nav_count)]
    if not all(math.isfinite(value) for point in markers + navpoints for value in point):
        raise ValueError("Non-finite scene position")
    return {"tracks": tracks, "markers": markers, "navpoints": navpoints}


def _multiply(a, b):
    return [[sum(a[row][k] * b[k][col] for k in range(4)) for col in range(4)]
            for row in range(4)]


def view_projection(frame: dict, aspect=4 / 3, near=0.05, far=625.0) -> dict:
    """Return column-major matrices for column vectors, left handed, Z 0..1.

    The source background's 640x384 pixels fill the original 640x480 viewport.
    right3/forward3 are world-space camera axes; ground controls must project
    them onto XZ and normalize them separately.
    """
    eye = frame["eye"]
    rotation = frame["rotation"]
    fov = frame["fov"]
    if (len(eye) != 3 or len(rotation) != 3
            or not all(math.isfinite(v) for v in [*eye, *rotation, fov, aspect, near, far])
            or not 0 < fov < math.pi or aspect <= 0 or not 0 < near < far):
        raise ValueError("Invalid camera or projection parameters")
    x, y, z = eye
    rx, ry, rz = (-value for value in rotation)
    cx, sx = math.cos(rx), math.sin(rx)
    cy, sy = math.cos(ry), math.sin(ry)
    cz, sz = math.cos(rz), math.sin(rz)
    translation = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [-x, -y, -z, 1]]
    rotate_x = [[1, 0, 0, 0], [0, cx, sx, 0], [0, -sx, cx, 0], [0, 0, 0, 1]]
    rotate_y = [[cy, 0, -sy, 0], [0, 1, 0, 0], [sy, 0, cy, 0], [0, 0, 0, 1]]
    rotate_z = [[cz, sz, 0, 0], [-sz, cz, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
    view = _multiply(_multiply(_multiply(translation, rotate_z), rotate_y), rotate_x)
    scale = 1 / math.tan(fov / 2)
    depth = far / (far - near)
    projection = [[scale / aspect, 0, 0, 0], [0, scale, 0, 0],
                  [0, 0, depth, 1], [0, 0, -near * depth, 0]]
    # Row-major D3D row-vector bytes equal column-major transposed matrices.
    flatten = lambda matrix: [value for row in matrix for value in row]
    return {
        "viewProjection16": flatten(_multiply(view, projection)),
        "view16": flatten(view),
        "projection16": flatten(projection),
        "eye3": list(eye),
        "right3": [view[row][0] for row in range(3)],
        "forward3": [view[row][2] for row in range(3)],
    }
