"""Decode original rigidly skinned PC meshes, skeletons and DDS textures."""

from io import BytesIO
import math
import struct


def _multiply(left, right):
    return [[sum(left[row][k] * right[k][column] for k in range(4))
             for column in range(4)] for row in range(4)]


def decode_mesh(data: bytes) -> list[dict]:
    if len(data) < 8:
        raise ValueError("Truncated mesh table")
    count = struct.unpack_from("<I", data)[0]
    if not count or count > (len(data) - 4) // 4:
        raise ValueError("Invalid mesh part count")
    offsets = list(struct.unpack_from(f"<{count}I", data, 4)) + [len(data)]
    if offsets[0] != 4 + count * 4:
        raise ValueError("Unexpected mesh table size")
    parts = []
    for start, end in zip(offsets, offsets[1:]):
        if start % 4 or end % 4 or end - start < 16 or end > len(data):
            raise ValueError("Invalid mesh part range")
        flags, vertices, assignments, triangles = struct.unpack_from("<4I", data, start)
        if not vertices or assignments != vertices:
            raise ValueError("Unsupported vertex/bone assignment counts")
        payload_end = start + 16 + vertices * 36 + assignments * 2 + triangles * 6
        if (payload_end + 3) & ~3 != end:
            raise ValueError("Mesh part counts exceed its range")
        records = list(struct.iter_unpack("<6fI2f", data[start + 16:start + 16 + vertices * 36]))
        if any(not math.isfinite(value) for record in records for value in (*record[:6], *record[7:])):
            raise ValueError("Non-finite mesh coordinate")
        bone_offset = start + 16 + vertices * 36
        bone_indices = list(struct.unpack_from(f"<{assignments}H", data, bone_offset))
        face_offset = bone_offset + assignments * 2
        faces = [list(face) for face in struct.iter_unpack("<3H", data[face_offset:payload_end])]
        if any(index >= vertices for face in faces for index in face):
            raise ValueError("Triangle index is outside the vertex array")
        parts.append({
            "flags_raw": flags,
            "positions": [list(record[:3]) for record in records],
            "normals": [list(record[3:6]) for record in records],
            "colors": [record[6] for record in records],
            "uv": [list(record[7:9]) for record in records],
            "bone_indices": bone_indices,
            "triangles": faces,
        })
    return parts


def decode_skeleton(data: bytes) -> list[dict]:
    if len(data) < 12:
        raise ValueError("Truncated skeleton header")
    count = struct.unpack_from("<I", data, 4)[0]
    if not count or 12 + count * 80 != len(data):
        raise ValueError("Invalid skeleton record count")
    bones = []
    for index in range(count):
        record = struct.unpack_from("<19fi", data, 12 + index * 80)
        if any(not math.isfinite(value) for value in record[:19]):
            raise ValueError("Non-finite skeleton transform")
        if record[19] < -1 or record[19] >= index:
            raise ValueError("Skeleton parent must precede its child")
        if any(abs(record[offset] - expected) > 1e-5 for offset, expected in ((3, 0), (7, 0), (11, 0), (15, 1))):
            raise ValueError("Unsupported non-affine skeleton matrix")
        bones.append({
            "matrix": [list(record[start:start + 4]) for start in range(0, 16, 4)],
            "translation": list(record[16:19]),
            "parent": record[19],
        })
    return bones


def pose_mesh(parts: list[dict], bones: list[dict]) -> list[dict]:
    """Apply the original local bind transforms; output coordinates are Y-up."""
    matrices = []
    for index, bone in enumerate(bones):
        parent = bone["parent"]
        if parent < -1 or parent >= index:
            raise ValueError("Skeleton parent must precede its child")
        matrix = [row[:] for row in bone["matrix"]]
        matrix[3][:3] = bone["translation"]
        if parent >= 0:
            matrix = _multiply(matrix, matrices[parent])
        matrices.append(matrix)
    posed = []
    for part in parts:
        positions, normals = [], []
        for position, normal, bone_index in zip(part["positions"], part["normals"], part["bone_indices"]):
            if not 0 <= bone_index < len(matrices):
                raise ValueError("Vertex bone index is outside the skeleton")
            matrix = matrices[bone_index]
            positions.append([sum(position[k] * matrix[k][column] for k in range(3)) + matrix[3][column]
                              for column in range(3)])
            transformed = [sum(normal[k] * matrix[k][column] for k in range(3)) for column in range(3)]
            length = math.sqrt(sum(value * value for value in transformed))
            normals.append([value / length for value in transformed] if length else transformed)
        posed.append({**part, "positions": positions, "normals": normals})
    return posed


def decode_animations(data: bytes) -> list[dict]:
    if len(data) < 8:
        raise ValueError("Truncated animation table")
    count = struct.unpack_from("<I", data)[0]
    if not count or count > (len(data) - 4) // 4:
        raise ValueError("Invalid animation count")
    offsets = list(struct.unpack_from(f"<{count}I", data, 4)) + [len(data)]
    if offsets[0] != 4 + count * 4:
        raise ValueError("Unexpected animation table size")
    clips = []
    for start, end in zip(offsets, offsets[1:]):
        if start % 4 or end % 4 or end - start < 40 or end > len(data):
            raise ValueError("Invalid animation range")
        resource_id, flags = struct.unpack_from("<2I", data, start)
        frames, bones, count36, count20 = struct.unpack_from("<4H", data, start + 20)
        curve_bytes = frames * (3 + bones * 3) * 4
        if not frames or not bones or 40 + curve_bytes + count36 * 36 + count20 * 20 != end - start:
            raise ValueError("Animation counts disagree with its range")
        values = struct.unpack_from(f"<{curve_bytes // 4}f", data, start + 40)
        if any(not math.isfinite(value) for value in values):
            raise ValueError("Non-finite animation coordinate")
        curve = lambda channel, frame: values[channel * frames + frame]
        event_offset = start + 40 + curve_bytes
        clips.append({
            "resource_id": resource_id,
            "flags_raw": flags,
            "frame_count": frames,
            "bone_count": bones,
            "root_positions": [[curve(axis, frame) for axis in range(3)] for frame in range(frames)],
            "rotations_degrees": [[[curve(3 + bone * 3 + axis, frame) for axis in range(3)]
                                   for frame in range(frames)] for bone in range(bones)],
            "events36_raw": [data[event_offset + index * 36:event_offset + (index + 1) * 36].hex()
                             for index in range(count36)],
            "events20_raw": [data[event_offset + count36 * 36 + index * 20:event_offset + count36 * 36 + (index + 1) * 20].hex()
                             for index in range(count20)],
        })
    return clips


def sample_pose(parts: list[dict], bones: list[dict], clip: dict, frame: int) -> list[dict]:
    """Sample an in-place source pose using the original renderer's root-Y replacement."""
    if clip["bone_count"] != len(bones):
        raise ValueError("Animation does not match the skeleton")
    if not isinstance(frame, int) or not 0 <= frame < clip["frame_count"]:
        raise ValueError("Animation frame is out of range")
    animated = []
    for index, bone in enumerate(bones):
        x, y, z = [math.radians(value) for value in clip["rotations_degrees"][index][frame]]
        cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
        rx = [[1, 0, 0, 0], [0, cx, sx, 0], [0, -sx, cx, 0], [0, 0, 0, 1]]
        ry = [[cy, 0, -sy, 0], [0, 1, 0, 0], [sy, 0, cy, 0], [0, 0, 0, 1]]
        rz = [[cz, sz, 0, 0], [-sz, cz, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
        rotation = _multiply(_multiply(rx, ry), rz)
        animated.append({**bone, "matrix": _multiply(rotation, bone["matrix"]),
                         "translation": [bone["translation"][0], clip["root_positions"][frame][1], bone["translation"][2]]
                         if index == 0 else bone["translation"]})
    return pose_mesh(parts, animated)


def decode_texture(data: bytes):
    from PIL import Image

    if len(data) < 136 or b"\0" not in data[4:132]:
        raise ValueError("Truncated texture name/header")
    source_name = data[4:132].split(b"\0", 1)[0].decode("ascii")
    size = struct.unpack_from("<I", data, 132)[0]
    if size != len(data) - 136 or data[136:140] != b"DDS ":
        raise ValueError("Invalid embedded DDS range")
    with Image.open(BytesIO(data[136:])) as image:
        return image.convert("RGBA"), source_name
