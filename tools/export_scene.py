"""Build the first native scene from the user's unmodified PC game data."""

import argparse
from collections import Counter
from fractions import Fraction as F
from io import BytesIO
import json
import math
from pathlib import Path
import shutil
import struct
import wave

from ed_assets import decode_background, parse_archive
from ed_camera import parse_scene, view_projection
from ed_collision import parse_collision
from ed_doors import decode_doors
from ed_masks import decode_masks, depth_map
from ed_model import decode_animations, decode_mesh, decode_skeleton, decode_texture, sample_pose
from ed_portals import parse_portals
from ed_scripts import ScriptPackage
from ed_triggers import parse_triggers
from ed_inventory import export_runtime_inventory
from ed_audio import export_runtime_audio, EPISODE
from export_actor import export_actors


ROOT = Path(__file__).resolve().parents[1]
BODY_PARTS = (0, 1, 10)
# shortcut: 0.25 is prototype body clearance; calibrate it with original actor scripts later.
PLAYER_RADIUS = 0.25
WALKTHROUGH_NODES = (23, 17, 22, 5, 12, 13, 12, 5, 6, 7, 14, 9, 10,
                     9, 14, 8, 7, 6, 16, 18, 19, 21, 16, 17, 23)


def resource(data, resource_id):
    matches = [entry for entry in parse_archive(data) if entry.resource_id == resource_id]
    if len(matches) != 1:
        raise ValueError(f"Expected one resource {resource_id:08x}, found {len(matches)}")
    entry = matches[0]
    return data[entry.offset:entry.offset + entry.size]


def pack_pose(parts):
    vertices = []
    for part_index in BODY_PARTS:
        part = parts[part_index]
        for triangle in part["triangles"]:
            for index in triangle:
                x, y, z = part["positions"][index]
                values = (x, y, z, *part["normals"][index], *part["uv"][index])
                if not all(math.isfinite(value) for value in values):
                    raise ValueError("Non-finite posed vertex")
                vertices.append(struct.pack("<8f", *values))
    return b"".join(vertices)


def ground_axis(axis):
    x, _, z = axis
    length = math.hypot(x, z)
    if length < 1e-6:
        raise ValueError("Camera axis cannot be projected onto the ground")
    return [x / length, 0, z / length]


def pack_camera_frames(tracks):
    """EDCAMF1: track counts, then29f records (VP,basisXZ,eye,right,up)."""
    if not tracks or len(tracks) > 256 or sum(len(track["frames"]) for track in tracks) > 100000:
        raise ValueError("Invalid camera frame track count")
    data = bytearray(struct.pack("<8sI", b"EDCAMF1\0", len(tracks)))
    for index, track in enumerate(tracks):
        if track["index"] != index or not 1 <= len(track["frames"]) <= 10000:
            raise ValueError("Camera frames must retain nonempty source track order")
        data.extend(struct.pack("<I", len(track["frames"])))
        for frame_index, frame in enumerate(track["frames"]):
            if frame["index"] != frame_index:
                raise ValueError("Camera frames must retain source frame order")
            camera = view_projection(frame)
            right, forward = ground_axis(camera["right3"]), ground_axis(camera["forward3"])
            up = [camera["view16"][row * 4 + 1] for row in range(3)]
            values = [*camera["viewProjection16"], right[0], right[2], forward[0], forward[2],
                      *camera["eye3"], *camera["right3"], *up]
            if len(values) != 29 or not all(math.isfinite(value) for value in values):
                raise ValueError("Invalid camera frame record")
            data.extend(struct.pack("<29f", *values))
    return bytes(data)


def export_camera_frames(game, output, name, context, *, depth_patches=None):
    """Export every authored image/depth pair; retain the legacy frame0 names."""
    game, output = Path(game).resolve(), Path(output).resolve()
    if output == game or game in output.parents:
        raise ValueError("Generated camera frames must be outside the original game directory")
    backgrounds = (game / f"data/{name}v.pc").read_bytes()
    entries = [entry for entry in parse_archive(backgrounds)
               if entry.resource_id >> 24 == 8 and entry.flags == 0]
    tracks, cameras = context["scene"]["tracks"], []
    output.mkdir(parents=True, exist_ok=True)
    for track_index, track in enumerate(tracks):
        for frame_index, frame in enumerate(track["frames"]):
            entry = entries[frame["background_index"]]
            payload = backgrounds[entry.offset:entry.offset + entry.size]
            image, source_name, tail_offset = decode_background(payload)
            layers = decode_masks(payload, tail_offset, scene_id=context["id"],
                                  background_index=frame["background_index"], depth_patches=depth_patches)
            suffix = f"{track_index}_{frame_index}" if frame_index else str(track_index)
            image.save(output / f"background_{suffix}.bmp")
            (output / f"background_{suffix}.depth").write_bytes(depth_map(layers))
            if frame_index == 0:
                cameras.append({"track": track_index, "frame": 0, "background": source_name,
                                "background_entry": entry.index, "foreground_layers": len(layers), **view_projection(frame)})
    (output / "camera-frames.bin").write_bytes(pack_camera_frames(tracks))
    return cameras


def floor_outline(floor):
    """Return the single contour used by the cabin's flat floor."""
    heights = [point[1] for point in floor["vertices"]]
    if not heights or max(heights) - min(heights) > 1e-6:
        raise ValueError("The first scene requires a flat floor")
    edges = Counter()
    for triangle in floor["triangles"]:
        points = [tuple(floor["vertices"][index][::2]) for index in triangle]
        for a, b in zip(points, points[1:] + points[:1]):
            if a == b:
                raise ValueError("Degenerate floor edge")
            edges[tuple(sorted((a, b)))] += 1
    neighbors = {}
    for (a, b), count in edges.items():
        if count > 2:
            raise ValueError("Non-manifold floor edge")
        if count == 1:
            neighbors.setdefault(a, []).append(b)
            neighbors.setdefault(b, []).append(a)
    if not neighbors or any(len(adjacent) != 2 for adjacent in neighbors.values()):
        raise ValueError("Floor boundary is not a closed contour")
    start = min(neighbors)
    contour, previous, current = [], None, start
    while current not in contour:
        contour.append(current)
        following = next(point for point in neighbors[current] if point != previous)
        previous, current = current, following
    if current != start or len(contour) != len(neighbors):
        raise ValueError("Multiple floor contours are not supported in the first scene")
    return [list(point) for point in contour], heights[0]


def pack_collision(collision):
    polygons = [(3, [item["circle_xz"]["center"], [item["circle_xz"]["radius"], 0]])
                if "circle_xz" in item else (0, item["vertices_xz"]) for item in collision["solids"]]
    floor = collision["floor"]
    heights = [point[1] for point in floor["vertices"]]
    if max(heights) - min(heights) < 1e-6:
        floor_height = heights[0]
        try:
            boundary, _ = floor_outline(floor)
        except ValueError:
            # Disconnected authored floor contours (AU01) retain their actual
            # edges through the existing segment format; never join islands.
            polygons.extend((2, [a[::2], b[::2]]) for a, b in floor_boundary_edges(floor))
        else:
            polygons.append((1, boundary))
    else:
        floor_height = None
        polygons.extend((2, [a[::2], b[::2]]) for a, b in floor_boundary_edges(floor))
    data = bytearray(struct.pack("<8sIf", b"EDCOLL1\0", len(polygons), PLAYER_RADIUS))
    for mode, points in polygons:
        data.extend(struct.pack("<2I", mode, len(points)))
        for point in points:
            data.extend(struct.pack("<2f", *point))
    return bytes(data), floor_height


def floor_boundary_edges(floor):
    def sub(a,b): return (a[0]-b[0], a[1]-b[1])
    def cross(a,b): return a[0]*b[1]-a[1]*b[0]
    def xz(p): return (F(p[0]),F(p[2]))
    triangles=[];edges={};faces=set();edge_counts=Counter()
    for indices in floor['triangles']:
        p=[tuple(floor['vertices'][i]) for i in indices]
        projected=[xz(v) for v in p]
        area=cross(sub(projected[1],projected[0]),sub(projected[2],projected[0]))
        face=tuple(sorted(p))
        if not area or face in faces: continue
        faces.add(face);triangles.append((projected,1 if area>0 else -1))
        for i in range(3):
            a,b,c=p[i],p[(i+1)%3],p[(i+2)%3]
            key=tuple(sorted((a,b)));edges.setdefault(key,(a,b,c));edge_counts[key]+=1
    if any(count>2 for count in edge_counts.values()):
        raise ValueError("Non-manifold floor edge")
    result=[];seen=set()
    for a3,b3,owner3 in edges.values():
        a,b,owner=xz(a3),xz(b3),xz(owner3);direction=sub(b,a)
        own_side=cross(direction,sub(owner,a));covered=[]
        for triangle,sign in triangles:
            lo,hi=F(0),F(1);boundaries=[]
            for i in range(3):
                p,q=triangle[i],triangle[(i+1)%3];edge=sub(q,p)
                alpha=sign*cross(edge,sub(a,p));beta=sign*cross(edge,direction)
                if not beta:
                    if alpha<0: hi=F(-1);break
                    if not alpha: boundaries.append((alpha,beta))
                elif beta>0: lo=max(lo,-alpha/beta)
                else: hi=min(hi,-alpha/beta)
            if lo>=hi: continue
            # Strict interior coverage removes an internal edge. On a shared
            # line remove only when the other triangle occupies the opposite
            # side; same-side/stacked outer edges must remain.
            if boundaries and not any(own_side*cross(direction,sub(v,a))<0 for v in triangle):continue
            covered.append((lo,hi))
        begin=F(0);remaining=[]
        for lo,hi in sorted(covered):
            if begin<lo:remaining.append((begin,lo))
            begin=max(begin,hi)
        if begin<1:remaining.append((begin,F(1)))
        for lo,hi in remaining:
            def interpolate(t):return tuple(float(F(a3[k])+(F(b3[k])-F(a3[k]))*t) for k in range(3))
            p,q=interpolate(lo),interpolate(hi)
            # EDCOLL1 stores float32 XZ. A rational intersection can round to
            # its adjacent original vertex; omit only that exact zero segment.
            def stored(v):
                x,z=struct.unpack('<2f',struct.pack('<2f',v[0],v[2]))
                return (x,v[1],z)
            p,q=sorted((stored(p),stored(q)));key=tuple(sorted((p[::2],q[::2])))
            if p[::2]!=q[::2] and key not in seen:seen.add(key);result.append((p,q))
    return result


def pack_floor(floor):
    data = bytearray(struct.pack("<8sI", b"EDFLOOR1", len(floor["triangles"])))
    for triangle in floor["triangles"]:
        for index in triangle:
            data.extend(struct.pack("<3f", *floor["vertices"][index]))
    return bytes(data)


def source_floor_height(floor, x, z):
    for triangle in floor["triangles"]:
        a, b, c = [floor["vertices"][index] for index in triangle]
        denominator = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
        if abs(denominator) < 1e-12:
            continue
        u = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / denominator
        v = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / denominator
        if min(u, v, 1 - u - v) >= -1e-6:
            return u * a[1] + v * b[1] + (1 - u - v) * c[1]
    raise ValueError("Source position has no floor triangle")


def pack_triggers(triggers):
    grid = triggers["spatial_grid"]
    conditional = triggers.get("requires_camera_events", False)
    data = bytearray(struct.pack("<8sI4f", b"EDTRIG3\0" if conditional else b"EDTRIG2\0", len(triggers["triggers"]),
                                 *grid["grid_origin_xz"], *grid["grid_extent_xz"]))
    zones = {zone["index"]: zone for zone in triggers["zones"]}
    for trigger in triggers["triggers"]:
        zone = zones[trigger["zone_index"]]
        actions = trigger["camera_actions"]
        exit_actions = trigger.get("exit_camera_actions", [])
        mask = zone["spatial_mask"] if zone["enabled"] else 0
        if conditional:
            rules = [rule for rule in trigger["rules"] if any("camera_index" in a for a in rule["actions"])]
            data.extend(struct.pack("<4I", zone["shape"], len(zone["points"]), len(rules), mask))
            for x, _, z in zone["points"]:
                data.extend(struct.pack("<2f", x, z))
            for rule in rules:
                event = {"enter": 1, "exit": 2}[rule["event"]]
                condition = rule.get("camera_event")
                if condition and condition != {"value": condition["value"], "requires_nonzero_payload": True}:
                    raise ValueError("Unsupported camera event predicate")
                cameras = [action["camera_index"] for action in rule["actions"] if "camera_index" in action]
                data.extend(struct.pack("<4I", event, 1 if condition else 0,
                                        condition["value"] if condition else 0, len(cameras)))
                data.extend(struct.pack(f"<{len(cameras)}I", *cameras))
            continue
        data.extend(struct.pack("<5I", zone["shape"], len(zone["points"]), len(actions), len(exit_actions), mask))
        for x, _, z in zone["points"]:
            data.extend(struct.pack("<2f", x, z))
        data.extend(struct.pack(f"<{len(actions)}I", *actions))
        data.extend(struct.pack(f"<{len(exit_actions)}I", *exit_actions))
    return bytes(data)


def embedded_wave(archive, resource_id):
    for entry in parse_archive(archive):
        if entry.flags != 1 or entry.resource_id >> 24 != 14:
            continue
        chunk = archive[entry.offset:entry.offset + entry.size]
        if len(chunk) < 4:
            raise ValueError("Truncated sound bundle")
        count = struct.unpack_from("<I", chunk)[0]
        if count > (len(chunk) - 4) // 12:
            raise ValueError("Sound table exceeds bundle")
        for index in range(count):
            identity, offset, _ = struct.unpack_from("<3I", chunk, 4 + index * 12)
            if identity != resource_id:
                continue
            end = struct.unpack_from("<I", chunk, 4 + (index + 1) * 12 + 4)[0] if index + 1 < count else len(chunk)
            if offset < 4 + count * 12 or end > len(chunk) or end - offset < 12:
                raise ValueError("Invalid embedded sound range")
            size = struct.unpack_from("<I", chunk, offset + 4)[0] + 8
            data = chunk[offset:offset + size]
            if size > end - offset or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
                raise ValueError("Invalid embedded RIFF sound")
            try:
                with wave.open(BytesIO(data), "rb") as sound:
                    if sound.getcomptype() != "NONE":
                        raise ValueError("Door sound must be original PCM")
            except wave.Error as error:
                raise ValueError("Invalid embedded door WAV") from error
            return data
    raise ValueError(f"Missing source sound {resource_id:08x}")


def export_font(game, output):
    # Original 0x4095AB passes 18x24 cells, 14 columns and this character table.
    executable = (game / "EvilDead.exe").read_bytes()
    prefix = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789?!.,;:+-="
    if executable.count(prefix) != 1:
        raise ValueError("Original font character table is missing or ambiguous")
    start = executable.index(prefix)
    end = executable.find(b"\0", start, start + 128)
    if end < 0:
        raise ValueError("Unterminated original font table")
    charset = executable[start:end].decode("ascii")
    shutil.copyfile(game / "data/font5.bmp", output / "font.bmp")
    (output / "font.txt").write_text(f"EDFONT1\n18 24 14\n{charset}\n", encoding="ascii")


def pack_doors(manifest):
    records = [(door["portal"], door) for door in manifest["doors"]]
    records.extend((portal, None) for portal in manifest["unsupported_portals"])
    data = bytearray(struct.pack("<8s4sI", b"EDDOOR1\0", manifest["scene_name"].encode("ascii"), len(records)))

    def geometry(zone):
        data.extend(struct.pack("<3I", zone["shape"], len(zone["points"]), zone["spatial_mask"]))
        for x, _, z in zone["points"]:
            data.extend(struct.pack("<2f", x, z))

    for portal, door in records:
        data.extend(struct.pack("<I", portal["index"]))
        if door:
            geometry(door["interaction_zone"])
            gate, timing, target = door["gate"], manifest["timing"], door["transition"]
            data.extend(struct.pack("<4f4I", *door["facing_markerXYZ"], gate["angle_argument_degrees"],
                                    gate["sound_resource_id"], timing["delay_ticks"],
                                    timing["latest_enable_ticks"], timing["input_guard_ticks"]))
            spawn = target["spawn_xyz"]
        else:
            # No decoded allowUse branch: remaining fields are non-executable metadata.
            geometry({"shape": 0, "points": [], "spatial_mask": 0})
            data.extend(struct.pack("<4f4I", 0, 0, 0, 0, 0, 0, 0, 0))
            spawn = [0, 0, 0]
        geometry(portal)
        data.extend(struct.pack("<4sI4f", portal["destination_scene"].encode("ascii"),
                                portal["camera_index"], *spawn, portal["yaw"]))
    return bytes(data)


def export_location(game, output, name, context, actor, shared_output, door_manifest, *,
                    depth_patches=None, spawn=None, spawn_role=None):
    scene, collision = context["scene"], context["collision"]
    collision_bytes, flat_height = pack_collision(collision)
    triggers = parse_triggers(context["payload"], camera_events=True)
    spawn = list(spawn if spawn is not None else scene["navpoints"][23] if name == "ak01" else scene["markers"][2])
    spawn[1] = source_floor_height(collision["floor"], spawn[0], spawn[2])
    lines = ["EDSCENE2", f"{actor['vertex_count']} {len(scene['tracks'])}", " ".join(map(str, spawn)), "1 1.3",
             f"{actor['idle_frames']} {actor['walk_frames']} 30"]
    output.mkdir(parents=True, exist_ok=True)
    for filename in ("ash.bmp", "ash.bin", "idle.bin", "walk.bin", "font.bmp", "font.txt", "door.wav"):
        shutil.copyfile(shared_output / filename, output / filename)
    cameras = export_camera_frames(game, output, name, context, depth_patches=depth_patches)
    for track_index, track in enumerate(scene["tracks"]):
        frame = track["frames"][0]
        camera = view_projection(frame)
        right, forward = ground_axis(camera["right3"]), ground_axis(camera["forward3"])
        lines.extend([" ".join(map(str, camera["viewProjection16"])), " ".join(map(str, right + forward))])
    (output / "scene.txt").write_text("\n".join(lines) + "\n", encoding="ascii")
    (output / "collision.bin").write_bytes(collision_bytes)
    (output / "triggers.bin").write_bytes(pack_triggers(triggers))
    (output / "floor.bin").write_bytes(pack_floor(collision["floor"]))
    (output / "scene.native").write_bytes(context["payload"])
    (output / "doors.bin").write_bytes(pack_doors(door_manifest))
    if name == "ak01":
        (output / "walkthrough.txt").write_text(walkthrough_path(collision), encoding="ascii")
    heights = [point[1] for point in collision["floor"]["vertices"]]
    metadata = {**actor, "scene": name, "scene_id": context["id"], "spawn": spawn,
                "spawn_role": spawn_role or ("source navigation point 23" if name == "ak01" else "default source portal arrival marker 2"),
                "viewport": [640, 480], "cameras": cameras,
                "camera_frames": {"file": "camera-frames.bin", "format": "EDCAMF1", "record_floats": 29,
                                  "frame_counts": [len(track["frames"]) for track in scene["tracks"]]},
                "collision": {"static_volume_count": len(collision["solids"]), "floor_height": flat_height,
                              "floor_height_range": [min(heights), max(heights)],
                              "floor_triangles": len(collision["floor"]["triangles"]), "radius": PLAYER_RADIUS,
                              "radius_source": "prototype clearance; original static collision uses a point",
                              "volume_indices": [item["volume_index"] for item in collision["solids"]]},
                "camera_triggers": {"count": len(triggers["triggers"]), "policy": "source enter/exit rules and order",
                                    "zone_indices": [item["zone_index"] for item in triggers["triggers"]]},
                "door_policy": door_manifest}
    if name == "ak01":
        metadata["walkthrough_navigation_nodes"] = list(WALKTHROUGH_NODES)
    (output / "source.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return metadata


def walkthrough_path(collision):
    navigation = collision["navigation"]
    links = {tuple(link) for link in navigation["links"]}
    if any((a, b) not in links for a, b in zip(WALKTHROUGH_NODES, WALKTHROUGH_NODES[1:])):
        raise ValueError("Walkthrough route does not follow the original navigation links")
    lines = ["EDPATH1", str(len(WALKTHROUGH_NODES))]
    for index in WALKTHROUGH_NODES:
        x, _, z = navigation["points"][index]
        lines.append(f"{x} {z} -1")
    return "\n".join(lines) + "\n"


def export_scripts(game: Path, output: Path):
    game, output = game.resolve(), output.resolve()
    if output == game or game in output.parents:
        raise ValueError("Generated scripts must be outside the original game directory")
    packages = {}
    for name in ("levelA", "levelB"):
        data = (game / f"data/script/pc/{name}.jpc").read_bytes()
        package = ScriptPackage(data)
        for method in package.methods:
            for instruction in package.disassemble(method["index"]):
                if instruction["opcode"] in (0x12, 0x13) and instruction["constant"]["index_raw"] < 0:
                    package.literal_string(instruction["constant"]["value_raw"])
        packages[name] = package
    output.mkdir(parents=True, exist_ok=True)
    for name, package in packages.items():
        (output / f"{name}.jpc").write_bytes(package.data)
    return packages


def export_shared_assets(game: Path, output: Path, *, level="A", audio_scenes=EPISODE, include_actors=True):
    """Shared original player/UI assets, without manufacturing a scene entry."""
    game, output = game.resolve(), output.resolve()
    if output == game or game in output.parents:
        raise ValueError("Generated assets must be outside the original game directory")
    if level not in ("A", "B"):
        raise ValueError("Unknown source level resource profile")
    shared_name = "az01.pc" if level == "A" else "bz01.pc"
    shared = (game / "data" / shared_name).read_bytes()
    parts = decode_mesh(resource(shared, 0x00024000))
    bones = decode_skeleton(resource(shared, 0x00824000))
    texture, texture_name = decode_texture(resource(shared, 0x08024000))
    idle = decode_animations(resource(shared, 0x01024000))[0]
    walk = decode_animations(resource(shared, 0x01824184))[1]
    clips = {"idle": idle, "walk": walk}
    meshes = {}
    for name, clip in clips.items():
        if clip["bone_count"] != len(bones):
            raise ValueError("Animation does not match Ash's skeleton")
        meshes[name] = b"".join(
            pack_pose(sample_pose(parts, bones, clip, frame))
            for frame in range(clip["frame_count"])
        )
    first_frame = meshes["idle"][:len(meshes["idle"]) // idle["frame_count"]]
    vertex_count = len(first_frame) // 32
    if not vertex_count or len(meshes["walk"]) != vertex_count * 32 * walk["frame_count"]:
        raise ValueError("Animation frame geometry differs")

    output.mkdir(parents=True, exist_ok=True)
    texture.save(output / "ash.bmp")
    (output / "ash.bin").write_bytes(first_frame)
    for name, data in meshes.items():
        (output / f"{name}.bin").write_bytes(data)
    export_font(game, output)
    export_runtime_inventory(game, output)
    export_runtime_audio(game, output, audio_scenes)
    if include_actors:
        export_actors(game, output, profile=level)
    (output / "door.wav").write_bytes(embedded_wave((game / "data/ak01.pc").read_bytes(), 0x0E8A4ACA))
    actor = {
        "source_game": "game", "mesh": shared_name + ":00024000", "body_parts": list(BODY_PARTS),
        "texture": texture_name, "vertex_count": vertex_count,
        "idle_resource_id": f"{idle['resource_id']:08x}", "idle_frames": idle["frame_count"],
        "walk_resource_id": f"{walk['resource_id']:08x}", "walk_frames": walk["frame_count"],
        "animation_fps": 30, "animation_fps_status": "prototype playback rate; original tick not established",
        "root_motion": "original renderer's in-place root-Y replacement; input drives world XZ",
    }
    packages = export_scripts(game, output / "scripts")
    return actor, packages


def export_scene(game: Path, output: Path):
    actor, packages = export_shared_assets(game, output)
    package = packages["levelA"]
    contexts, destinations = {}, {}
    for name in ("ak01", "ak09"):
        archive = (game / f"data/{name}.pc").read_bytes()
        entries = [entry for entry in parse_archive(archive) if entry.resource_id >> 24 == 15]
        if len(entries) != 1:
            raise ValueError("Expected one source scene resource")
        entry = entries[0]
        payload = archive[entry.offset:entry.offset + entry.size]
        scene_id = struct.unpack_from("<I", archive)[0]
        contexts[name] = {"id": scene_id, "payload": payload, "scene": parse_scene(payload),
                          "collision": parse_collision(payload)}
        destinations[scene_id] = parse_portals(payload, scene_id)
    results = {}
    for name, context in contexts.items():
        doors = decode_doors(context["payload"], context["id"], package, destinations)
        results[name] = export_location(game, output / name, name, context, actor, output, doors)
    # Keep the original single-scene test entry point alongside the two-scene world.
    for path in (output / "ak01").iterdir():
        if path.is_file():
            shutil.copyfile(path, output / path.name)
    (output / "world.txt").write_text("EDWORLD1\nak01\n2\nak01\nak09\n", encoding="ascii")
    return results["ak01"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--output", type=Path, default=ROOT / "build/doors/assets")
    args = parser.parse_args()
    try:
        metadata = export_scene(args.game, args.output)
    except (OSError, ValueError, IndexError) as error:
        parser.exit(1, f"Error: {error}\n")
    print(f"Exported Ash: {metadata['vertex_count']} vertices, "
          f"{metadata['idle_frames']} idle / {metadata['walk_frames']} walk frames, "
          f"{len(metadata['cameras'])} cameras")
    print(args.output.resolve())


if __name__ == "__main__":
    main()
