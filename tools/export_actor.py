"""Export compact original rigid skinning data, all actor curves and action tables."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

from ed_actions import NativeActions, encode_actions, parse_actor_actions
from ed_assets import parse_archive, parse_dc_archive
from ed_model import decode_animations, decode_mesh, decode_skeleton, decode_texture, sample_pose


ROOT = Path(__file__).resolve().parents[1]
ACTORS = {
    "ash": {"archive": "az01.pc", "mesh": 0x00024000, "skeleton": 0x00824000,
            "texture": 0x08024000, "parts": (0, 1, 10), "packs": (0x01024000, 0x01824184),
            "full_golden_states": (133, 146, 147)},
    "deadite": {"archive": "az01.pc", "mesh": 0x040C6000, "skeleton": 0x048C6000,
                "texture": 0x080C6000, "parts": (0,), "packs": (0x050C6000, 0x058C6284),
                "full_golden_states": (10, 22, 24)},
    "skullbat": {"archive": "az01.pc", "mesh": 0x04482000, "skeleton": 0x04C82000,
                 "texture": 0x08482000, "parts": (0,), "packs": (0x05482000, 0x05C82F04),
                 "full_golden_states": (4, 7, 8)},
    "skeleton": {"archive": "al02.pc", "mesh": 0x06494000, "skeleton": 0x06C94000,
                 "texture": 0x08494000, "parts": (0,), "packs": (0x07494000, 0x07C940C4),
                 "full_golden_states": (15, 24, 28)},
    "hellbillyfat": {"archive": "ah05.pc", "mesh": 0x061CA000, "skeleton": 0x069CA000,
                    "texture": 0x081CA000, "parts": (0,), "packs": (0x071CA000, 0x079CA0C4),
                    "full_golden_states": (4, 7, 11)},
    "hellbillythin": {"archive": "ah05.pc", "mesh": 0x061D8000, "skeleton": 0x069D8000,
                     "texture": 0x081D8000, "parts": (0,), "packs": (0x071D8000, 0x079D8284),
                     "full_golden_states": (6, 10, 14)},
    "demonpig": {"archive": "ah05.pc", "mesh": 0x063CC000, "skeleton": 0x06BCC000,
                 "texture": 0x083CC000, "parts": (0,), "packs": (0x073CC000, 0x07BCC284),
                 "full_golden_states": (10, 14, 16)},
    "headlessskeleton": {"archive": "ak09.pc", "mesh": 0x06082000, "skeleton": 0x06882000,
                         "texture": 0x08082000, "parts": (0,), "packs": (0x07082000, 0x078820C4),
                         "full_golden_states": (10, 16, 22)},
    "troopknife": {"archive": "ac01.pc", "mesh": 0x06318000, "skeleton": 0x06B18000,
                   "texture": 0x08318000, "parts": (0,), "packs": (0x07318000, 0x07B18504),
                   "full_golden_states": (6, 12, 16)},
    "troopclaw": {"archive": "ac01.pc", "mesh": 0x06304000, "skeleton": 0x06B04000,
                  "texture": 0x08304000, "parts": (0,), "packs": (0x07304000, 0x07B04504),
                  "full_golden_states": (6, 12, 16)},
    "trooprange": {"archive": "ac01.pc", "mesh": 0x06300000, "skeleton": 0x06B00000,
                   "texture": 0x08300000, "parts": (0,), "packs": (0x07300000, 0x07B00504),
                   "full_golden_states": (8, 10, 14)},
    "treeboss": {"archive": "at02.pc", "mesh": 0x024E2000, "skeleton": 0x02CE2000,
                 "texture": 0x084E2000, "parts": (0, 1, 2, 3), "packs": (0x034E2000, 0x03CE2284),
                 "full_golden_states": (2, 4, 5)},
    "treearm": {"archive": "at02.pc", "mesh": 0x024C0000, "skeleton": 0x02CC0000,
                "texture": 0x084C0000, "parts": (0,), "packs": (0x034C0000, 0x03CC0F04),
                "full_golden_states": (0, 1, 2)},
    "troopleaderboss": {"archive": "ac05.pc", "mesh": 0x02496000, "skeleton": 0x02C96000,
                        "texture": 0x08496000, "parts": (0,), "packs": (0x03496000, 0x03C960C4),
                        "full_golden_states": (14, 20, 21)},
    "matriarchboss": {"archive": "ah06.pc", "mesh": 0x02310000, "skeleton": 0x02B10000,
                      "texture": 0x08310000, "parts": (0,), "packs": (0x03310000, 0x03B10144),
                      "full_golden_states": (8, 16, 18)},
    "deadash": {"archive": "ag03.pc", "mesh": 0x020C0000, "skeleton": 0x028C0000,
                "texture": 0x080C0000, "parts": (0, 1), "packs": (0x030C0000, 0x038C0284),
                "full_golden_states": (12, 13, 23)},
    "anniezombie": {"archive": "au04.pc", "mesh": 0x02032000, "skeleton": 0x02832000,
                    "texture": 0x08032000, "parts": (0, 1, 2), "packs": (0x03032000, 0x03832284),
                    "full_golden_states": (1, 12, 21)},
}

# Explicit LevelB resource profile. The default ACTORS/export_actors profile is
# still LevelA; these assets never become implicit A runtime requirements.
LEVEL_B_ACTORS = {
    "ash": {**ACTORS["ash"], "archive": "bz01.pc", "native_type": 0, "source_class": "Ash", "capacity": 1, "prototypes": ()},
    "deadite": {**ACTORS["deadite"], "archive": "bz01.pc", "native_type": 4, "source_class": "Deadite", "capacity": 4, "prototypes": (4,)},
    "skullbat": {**ACTORS["skullbat"], "archive": "bz01.pc", "native_type": 2, "source_class": "SkullBat", "capacity": 4, "prototypes": (2,)},
    "badash": {"archive": "bc01.pc", "mesh": 0x06040000, "skeleton": 0x06840000, "texture": 0x08040000,
               "parts": (0,), "packs": (0x07040000, 0x078400C4), "full_golden_states": (0, 11, 20),
               "native_type": 19, "source_class": "BadAsh", "capacity": 1, "prototypes": (19,)},
    "guildmember": {"archive": "bl06.pc", "mesh": 0x06198000, "skeleton": 0x06998000, "texture": 0x08198000,
                    "parts": (0,), "packs": (0x07198000, 0x07998A04), "full_golden_states": (0, 7, 25),
                    "native_type": 23, "source_class": "GuildMember", "capacity": 4, "prototypes": (23,)},
    "merchant": {"archive": "bm11.pc", "mesh": 0x06326000, "skeleton": 0x06B26000, "texture": 0x08326000,
                 "parts": (0,), "packs": (0x07326000, 0x07B26144), "full_golden_states": (0, 1, 3),
                 "native_type": 24, "source_class": "Merchant", "capacity": 4, "prototypes": (24,)},
    "wizardboss": {"archive": "bt11.pc", "mesh": 0x025B2000, "skeleton": 0x02DB2000, "texture": 0x085B2000,
                   "parts": (0,), "packs": (0x035B2000, 0x03DB2784), "full_golden_states": (0, 6, 14),
                   "native_type": 26, "source_class": "WizardBoss", "capacity": 1, "prototypes": (26,)},
    "mudgolem": {"archive": "ba04.pc", "mesh": 0x0230C000, "skeleton": 0x02B0C000, "texture": 0x0830C000,
                 "parts": (0,), "packs": (0x0330C000, 0x03B0C0C4), "full_golden_states": (0, 2, 9),
                 "native_type": 27, "source_class": "MudGolem", "capacity": 1, "prototypes": (27,)},
    "armoryskeleton": {"archive": "bg03.pc", "mesh": 0x06480000, "skeleton": 0x06C80000, "texture": 0x08480000,
                       "parts": (0,), "packs": (0x07480000, 0x07C800C4), "full_golden_states": (0, 14, 31),
                       "native_type": 28, "source_class": "ArmorySkeleton", "capacity": 4, "prototypes": (28,)},
    "armoryboss": {"archive": "bg12.pc", "mesh": 0x02002000, "skeleton": 0x02802000, "texture": 0x08002000,
                   "parts": (0,), "packs": (0x03002000, 0x03802504), "full_golden_states": (0, 8, 12),
                   "native_type": 29, "source_class": "ArmoryBoss", "capacity": 1, "prototypes": (29,)},
    "netherpet": {"archive": "ba11.pc", "mesh": 0x0635E000, "skeleton": 0x06B5E000, "texture": 0x0835E000,
                  "parts": (0,), "packs": (0x0735E000, 0x07B5E0C4, 0x07B620C4), "full_golden_states": (0, 4, 16),
                  "native_type": 31, "source_class": "NetherPet", "capacity": 4, "prototypes": (31,)},
    "cthulhu": {"archive": "bc02.pc", "mesh": 0x020A8000, "skeleton": 0x028A8000, "texture": 0x080A8000,
                "parts": (0,), "packs": (0x030A8000, 0x038A8284), "full_golden_states": (0, 2, 6),
                "native_type": 33, "source_class": "Cthulhu", "capacity": 1, "prototypes": (33,)},
    "captain": {"archive": "bg08.pc", "mesh": 0x0608C000, "skeleton": 0x0688C000, "texture": 0x0808C000,
                "parts": (0,), "packs": (0x0708C000, 0x0788C0C4), "full_golden_states": (0, 8, 27),
                "native_type": 38, "source_class": "Captain", "capacity": 1, "prototypes": (38,)},
    "pumpkeeper": {"archive": "ba02.pc", "mesh": 0x063D4000, "skeleton": 0x06BD4000, "texture": 0x083D4000,
                   "parts": (0,), "packs": (0x073D4000, 0x07BD40C4), "full_golden_states": (0, 6, 18),
                   "native_type": 39, "source_class": "PumpKeeper", "capacity": 1, "prototypes": (39,)},
}
# ActorDB prototypes36/37 share native pool31 and its exact FSM/curve IDs.
# Their original meshes/skeletons/textures remain separate asset identities.
for _name, _prototype, _mesh in (("netherpet_06360000", 36, 0x06360000), ("netherpet_06362000", 37, 0x06362000)):
    LEVEL_B_ACTORS[_name] = {**LEVEL_B_ACTORS["netherpet"], "mesh": _mesh, "skeleton": _mesh + 0x00800000,
                           "texture": _mesh + 0x02000000, "action_actor": "netherpet", "prototypes": (_prototype,)}
B_STARTUP_ACTORS = ("ash", "deadite", "skullbat")


def resource(blob, identity):
    entries = [entry for entry in parse_archive(blob) if entry.resource_id == identity]
    if len(entries) != 1:
        raise ValueError(f"Expected one source actor resource {identity:08x}")
    entry = entries[0]
    return blob[entry.offset:entry.offset + entry.size]


def flatten_vertices(parts, selected_parts):
    vertices = []
    for index in selected_parts:
        if not isinstance(index, int) or not 0 <= index < len(parts):
            raise ValueError("Selected actor part is outside source mesh")
        part = parts[index]
        for triangle in part["triangles"]:
            for vertex in triangle:
                values = (*part["positions"][vertex], *part["normals"][vertex], *part["uv"][vertex])
                if len(values) != 8 or not all(math.isfinite(value) for value in values):
                    raise ValueError("Invalid actor vertex")
                vertices.append((values, part["bone_indices"][vertex]))
    return vertices


def encode_actor(parts, bones, clips, selected_parts, *, part_tags=False):
    """EDACTOR1: counts; <8fHH> vertices; <19fi> bones; original curve frames.

    Each clip is <3I> ID/flags/frameCount, followed by frame-major rootXYZ3f
    and boneCount EulerXYZ3f records in degrees. Curves retain their original
    array indices; the native action scheduler applies clipFlags&2 remapping.
    """
    vertices = flatten_vertices(parts, selected_parts)
    if not 1 <= len(vertices) <= 1000000 or not 1 <= len(bones) <= 256 or not 1 <= len(clips) <= 4096:
        raise ValueError("Invalid compact actor counts")
    tags = [index for index in selected_parts for _ in range(len(parts[index]["triangles"]) * 3)]
    data = bytearray(struct.pack("<8s3I", b"EDACTOR2" if part_tags else b"EDACTOR1", len(vertices), len(bones), len(clips)))
    for (values, bone), part in zip(vertices, tags):
        if not 0 <= bone < len(bones):
            raise ValueError("Actor vertex references a missing bone")
        if part_tags and not 0 <= part < 256:
            raise ValueError("Actor part index exceeds native visibility table")
        data.extend(struct.pack("<8fHH", *values, bone, part if part_tags else 0))
    for index, bone in enumerate(bones):
        matrix = [value for row in bone["matrix"] for value in row]
        values = [*matrix, *bone["translation"]]
        if len(values) != 19 or not all(math.isfinite(value) for value in values) or not -1 <= bone["parent"] < index:
            raise ValueError("Invalid actor skeleton transform or parent")
        if any(abs(matrix[offset] - expected) > 1e-5 for offset, expected in ((3, 0), (7, 0), (11, 0), (15, 1))):
            raise ValueError("Actor skeleton must be affine")
        data.extend(struct.pack("<19fi", *values, bone["parent"]))
    identities = set()
    for clip in clips:
        identity, frames = clip["resource_id"], clip["frame_count"]
        if identity in identities or not identity or not 1 <= frames <= 65535 or clip["bone_count"] != len(bones):
            raise ValueError("Invalid actor clip identity, frame or bone count")
        identities.add(identity)
        data.extend(struct.pack("<3I", identity, clip["flags_raw"], frames))
        for frame in range(frames):
            values = [*clip["root_positions"][frame],
                      *(value for bone in clip["rotations_degrees"] for value in bone[frame])]
            if len(values) != 3 + len(bones) * 3 or not all(math.isfinite(value) for value in values):
                raise ValueError("Invalid actor curve")
            data.extend(struct.pack(f"<{len(values)}f", *values))
    return bytes(data)


def encode_animation_events(clips):
    """Preserve original clip36/20-byte records without changing compact curves."""
    data=bytearray(struct.pack("<8sI",b"EDANEV1\0",len(clips)))
    seen=set()
    for clip in clips:
        identity=clip["resource_id"]
        if not identity or identity in seen:raise ValueError("Duplicate original event clip")
        seen.add(identity)
        records36=[bytes.fromhex(raw) for raw in clip["events36_raw"]]
        records20=[bytes.fromhex(raw) for raw in clip["events20_raw"]]
        if any(len(record)!=36 for record in records36) or any(len(record)!=20 for record in records20):
            raise ValueError("Invalid original animation event record size")
        data.extend(struct.pack("<3I",identity,len(records36),len(records20)))
        data.extend(b"".join(records36));data.extend(b"".join(records20))
    return bytes(data)


def pose_goldens(parts, bones, clips, selected_parts, full_clip_ids):
    """Bounded test artifact: all clips' endpoints/midpoint, per-bone samples.

    Use/hit and enemy attack/death representative clips include every triangle
    vertex. These values come from ed_model.sample_pose, independently of C++.
    """
    vertices = flatten_vertices(parts, selected_parts)
    sample_indices = {0, len(vertices) - 1}
    first_by_bone = {}
    for index, (_, bone) in enumerate(vertices):
        first_by_bone.setdefault(bone, index)
    sample_indices.update(first_by_bone.values())
    cases = []
    for clip in clips:
        frames = sorted({0, clip["frame_count"] // 2, clip["frame_count"] - 1})
        indices = list(range(len(vertices))) if clip["resource_id"] in full_clip_ids else sorted(sample_indices)
        for frame in frames:
            posed = flatten_vertices(sample_pose(parts, bones, clip, frame), selected_parts)
            cases.append((clip["resource_id"], frame, clip["root_positions"][frame],
                          [(index, posed[index][0]) for index in indices]))
    data = bytearray(struct.pack("<8sI", b"EDPOSE1\0", len(cases)))
    for identity, frame, root, points in cases:
        data.extend(struct.pack("<3I3f", identity, frame, len(points), *root))
        for index, values in points:
            data.extend(struct.pack("<I8f", index, *values))
    return bytes(data), len(cases)


def masked_hit_golden(parts, bones, clips, selected_parts, states):
    """One source Ash idle+hit layered pose, sampled through the Python skinner."""
    actions = NativeActions(states)
    if not actions.command(35):
        raise ValueError("Original idle unexpectedly rejected hit35")
    for _ in range(4):
        actions.tick()
    by_id = {clip["resource_id"]: clip for clip in clips}
    angles = [[0., 0., 0.] for _ in bones]
    root = list(bones[0]["translation"])
    layers = []
    for index, playback in enumerate(actions.playbacks):
        if not playback.active or playback.mode == 4:
            continue
        state = states[playback.state]
        frame = playback.frame
        if state["clip_flags"] & 2:
            frame = (frame or state["frame_count"]) - 1
        clip, mask = by_id[state["animation_id"]], state["bone_mask"]
        if not mask or mask[0]:
            root = clip["root_positions"][frame]
        for bone in range(len(bones)):
            if not mask or mask[bone]:
                angles[bone] = clip["rotations_degrees"][bone][frame]
        layers.append({"playback_index": index, "state": playback.state, "clip": state["animation_id"],
                       "curve_frame": frame, "mask": mask})
    if len(layers) != 2 or layers[1]["playback_index"] < 25:
        raise ValueError("Original hit35 no longer creates the verified priority layer")
    mixed = {"bone_count": len(bones), "frame_count": 1, "root_positions": [root],
             "rotations_degrees": [[value] for value in angles]}
    vertices = flatten_vertices(sample_pose(parts, bones, mixed, 0), selected_parts)
    samples = {}
    for index, (_, bone) in enumerate(vertices):
        samples.setdefault(bone, index)
    data = bytearray(struct.pack("<8sI", b"EDMIX1\0\0", len(layers)))
    for layer in layers:
        data.extend(struct.pack("<3I", layer["clip"], layer["curve_frame"], len(layer["mask"])))
        data.extend(bytes(layer["mask"]))
    data.extend(struct.pack("<I", len(samples)))
    for index in samples.values():
        data.extend(struct.pack("<I8f", index, *vertices[index][0]))
    return bytes(data), {"action": 35, "animation_updates": 4, "layers": layers,
                         "vertex_samples": len(samples)}


def export_actor(game, output, actor, *, definition=None, dreamcast=False):
    game, output = Path(game).resolve(), Path(output).resolve()
    if output == game or game in output.parents:
        raise ValueError("Generated actor assets must be outside the original game directory")
    if definition is None:
        definition = ACTORS.get(actor) or LEVEL_B_ACTORS.get(actor)
    if definition is None:
        raise ValueError("Unverified original actor definition")
    if dreamcast:
        from ed_dc_audio import checked_output
        from ed_dc_actions import parse_dc_actor_actions
        from ed_dc_model import decode_mesh as dc_mesh, decode_texture as dc_texture
        checked_output(game, output)
        archive_name = Path(definition["archive"]).stem.upper() + ".DC"
        archive = (game / archive_name).read_bytes()
        executable = (game / "1ST_READ.BIN").read_bytes()
        entries = parse_dc_archive(archive)
        def source_resource(identity):
            matches = [entry for entry in entries if entry.resource_id == identity]
            if len(matches) != 1:
                raise ValueError(f"Expected one Dreamcast actor resource {identity:08x}")
            entry = matches[0]
            return archive[entry.offset:entry.offset + entry.size]
        parts = dc_mesh(source_resource(definition["mesh"]))
        texture, source_texture = dc_texture(source_resource(definition["texture"]), definition["texture"])
        states = parse_dc_actor_actions(executable, archive, definition.get("action_actor", actor))
    else:
        archive_name = definition["archive"]
        archive = (game / "data" / archive_name).read_bytes()
        executable = (game / "EvilDead.exe").read_bytes()
        source_resource = lambda identity: resource(archive, identity)
        parts = decode_mesh(source_resource(definition["mesh"]))
        texture, source_texture = decode_texture(source_resource(definition["texture"]))
        states = parse_actor_actions(executable, archive, definition.get("action_actor", actor))
    bones = decode_skeleton(source_resource(definition["skeleton"]))
    clips = [clip for identity in definition["packs"] for clip in decode_animations(source_resource(identity))]
    clip_ids = {clip["resource_id"] for clip in clips}
    if any(state["animation_id"] and state["animation_id"] not in clip_ids for state in states):
        raise ValueError("Original action table references a missing actor clip")
    encoded = encode_actor(parts, bones, clips, definition["parts"])
    actions = encode_actions(states)
    full_clips = {states[index]["animation_id"] for index in definition["full_golden_states"]}
    goldens, golden_cases = pose_goldens(parts, bones, clips, definition["parts"], full_clips)
    directory = output / "actors" / actor
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "actor.bin").write_bytes(encoded)
    all_parts = encode_actor(parts, bones, clips, tuple(range(len(parts))), part_tags=True)
    (directory / "actor-parts.bin").write_bytes(all_parts)
    events=encode_animation_events(clips)
    (directory / "animation-events.bin").write_bytes(events)
    all_goldens, _ = pose_goldens(parts, bones, clips, tuple(range(len(parts))),
                                 {states[147]["animation_id"]} if actor == "ash" else set())
    (directory / "parts-pose-goldens.bin").write_bytes(all_goldens)
    (directory / "actions.bin").write_bytes(actions)
    (directory / "pose-goldens.bin").write_bytes(goldens)
    if actor == "troopleaderboss":
        # The CPU boss-puzzle proof queries the exact original pillar volumes.
        (directory / "source-scene.native").write_bytes(source_resource(0x0F022800))
    layered = None
    if actor == "ash":
        mixed, layered = masked_hit_golden(parts, bones, clips, definition["parts"], states)
        (directory / "pose-layer-golden.bin").write_bytes(mixed)
    texture.save(directory / "texture.bmp")
    if actor == "ash":
        (output / "actions.bin").write_bytes(actions)
    metadata = {"actor": actor, "archive": archive_name, "archive_sha256": hashlib.sha256(archive).hexdigest(),
                "executable_sha256": hashlib.sha256(executable).hexdigest(), "mesh": hex(definition["mesh"]),
                "skeleton": hex(definition["skeleton"]), "texture": hex(definition["texture"]),
                "texture_name": source_texture, "selected_parts": list(definition["parts"]),
                "vertex_count": len(flatten_vertices(parts, definition["parts"])), "bone_count": len(bones),
                "clip_count": len(clips), "source_frames": sum(clip["frame_count"] for clip in clips),
                "actor_bytes": len(encoded), "actor_sha256": hashlib.sha256(encoded).hexdigest(),
                "all_parts_format": "EDACTOR2", "all_parts_count": len(parts),
                "all_parts_vertices": sum(len(part["triangles"]) * 3 for part in parts),
                "all_parts_bytes": len(all_parts), "all_parts_sha256": hashlib.sha256(all_parts).hexdigest(),
                "actions_bytes": len(actions), "pose_golden_cases": golden_cases, "pose_golden_bytes": len(goldens),
                "animation_events_format":"EDANEV1","animation_events_bytes":len(events),"animation_events_sha256":hashlib.sha256(events).hexdigest(),
                "full_mesh_golden_clips": [hex(identity) for identity in sorted(full_clips)],
                "layered_pose_golden": layered,
                "clips": [{"id": clip["resource_id"], "flags": clip["flags_raw"], "frames": clip["frame_count"]} for clip in clips],
                "pose_policy": "Row vectors; Rx*Ry*Rz*bind; local translation replaces matrix row3; local*parent; rootY replaces bone0 translationY. RootXYZ curves remain available independently. Normalize transformed normals.",
                "layer_policy": "Apply active nonwaiting original playback slots in ascending order. Nonzero mask byte overwrites that bone's local Euler; mask0 also preserves rootXYZ. Keep unwritten local pose across updates; no invented crossfade.",
                "limits": "Selected visible mesh parts and original texture. Vertex colors are omitted from the existing Vertex32 shader contract. Native root-motion movement, actor effects and animation event payload execution are separate."}
    if "native_type" in definition:
        metadata.update(level="B", native_type=definition["native_type"], source_class=definition["source_class"],
                        native_capacity=definition["capacity"], source_prototypes=list(definition["prototypes"]),
                        action_family=definition.get("action_actor", actor),
                        resource_tuple=[definition[key] for key in ("mesh", "skeleton", "texture")])
    if dreamcast:
        metadata["platform"] = "Dreamcast"
        (directory / "source-texture.resource").write_bytes(source_resource(definition["texture"]))
    (directory / "source.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return metadata


def export_actors(game, output, *, profile="A"):
    game, output = Path(game).resolve(), Path(output).resolve()
    if output == game or game in output.parents:
        raise ValueError("Generated actor assets must be outside the original game directory")
    if profile not in ("A", "B", "B-startup"):
        raise ValueError("Unknown explicit actor resource profile")
    definitions = ACTORS if profile == "A" else LEVEL_B_ACTORS
    names = B_STARTUP_ACTORS if profile == "B-startup" else tuple(definitions)
    result={actor: export_actor(game, output, actor, definition=definitions[actor]) for actor in names}
    from ed_actor_textures import export_actor_textures
    export_actor_textures(game,output,default_ids={name:definitions[name]["texture"] for name in names})
    if profile != "A":
        levelb_actor_manifest(game, output, names)
    return result


def levelb_actor_manifest(game, output, names):
    """Validate B resource identities against original JPC factories/native pools."""
    game, output = Path(game).resolve(), Path(output).resolve()
    if output == game or game in output.parents:
        raise ValueError("LevelB actor metadata must be outside the original game directory")
    from audit_levelb_coverage import native_actor, script_capacities
    from audit_scene_coverage import literal_arguments
    from ed_actions import ACTORS as ACTION_TABLES, EXE_SHA256, _va_reader
    from ed_scripts import ScriptPackage
    data = (game / "data/script/pc/levelB.jpc").read_bytes()
    package = ScriptPackage(data)
    startup = next(m for m in package.methods if (m["class_name"], m["name"]) == ("LevelB", "create"))
    startup_code = package.disassemble(startup["index"])
    startup_calls = {}
    for index, row in enumerate(startup_code):
        target = row.get("method", {})
        if target.get("class_name") != "ActorDB" or target.get("name") not in ("createAsh", "createDeadite", "createSkullBat"):
            continue
        kind = {"createAsh": 0, "createDeadite": 4, "createSkullBat": 2}[target["name"]]
        startup_calls[kind] = {"kind": "direct LevelB.create factory", "native_type": kind,
                               "resource_tuple": literal_arguments(startup_code, index, 3),
                               "method": startup["index"], "call_pc": row["offset"], "factory": target["name"]}
    method = next(m for m in package.methods if (m["class_name"], m["name"]) == ("ActorDB", "createType"))
    code = package.disassemble(method["index"])
    offsets = {row["offset"]: index for index, row in enumerate(code)}
    switch = next(row for row in code if row["mnemonic"] == "lookupswitch")
    prototypes = {}
    for prototype, pc in switch["cases"]:
        index = next(index for index in range(offsets[pc], len(code)) if code[index].get("method", {}).get("class_name") == "ActorDB")
        call = code[index]["method"]
        args = literal_arguments(code, index, 4 if call["name"] == "create" else 3)
        if args is None:
            raise ValueError("LevelB actor resource arguments are not source literals")
        native_type = args[0] if call["name"] == "create" else {"createAsh": 0, "createDeadite": 4, "createSkullBat": 2}[call["name"]]
        prototypes[prototype] = {"native_type": native_type, "resource_tuple": args[-3:],
                                 "method": method["index"], "branch_pc": pc, "call_pc": code[index]["offset"]}
    create = next(m for m in package.methods if (m["class_name"], m["name"], m["descriptor"]) == ("ActorDB", "create", "(IIII)LGenericNpc;"))
    factory = package.disassemble(create["index"])
    factory_offsets = {row["offset"]: index for index, row in enumerate(factory)}
    classes = {kind: factory[factory_offsets[pc]]["class_name"]
               for kind, pc in next(row for row in factory if row["mnemonic"] == "lookupswitch")["cases"]}
    pools = script_capacities(package)
    executable = (game / "EvilDead.exe").read_bytes()
    if hashlib.sha256(executable).hexdigest() != EXE_SHA256:
        raise ValueError("LevelB actor metadata requires the verified original PC executable")
    read = _va_reader(executable)
    actors = []
    for name in names:
        definition = LEVEL_B_ACTORS[name]
        kind = definition["native_type"]
        native = native_actor(read, kind)
        family = definition.get("action_actor", name)
        if (native["capacity"] != definition["capacity"] or pools[kind]["capacity"] != definition["capacity"]
                or classes[kind] != definition["source_class"] or int(native["fsm_table"], 16) != ACTION_TABLES[family][0]
                or native["fsm_states"] != ACTION_TABLES[family][1]):
            raise ValueError("LevelB actor definition differs from source class/pool/FSM")
        source_calls = []
        if name in B_STARTUP_ACTORS:
            source = startup_calls[kind]
            if source["resource_tuple"] != [definition[key] for key in ("mesh", "skeleton", "texture")]:
                raise ValueError("LevelB startup resource tuple differs from original direct factory")
            source_calls.append(source)
        for prototype in definition["prototypes"]:
            source = prototypes[prototype]
            if source["native_type"] != kind or source["resource_tuple"] != [definition[key] for key in ("mesh", "skeleton", "texture")]:
                raise ValueError(f"LevelB actor {name}/prototype{prototype} resource tuple differs from original factory: {source}")
            source_calls.append({"prototype": prototype, **source})
        metadata = json.loads((output / "actors" / name / "source.json").read_text(encoding="utf-8"))
        actors.append({"asset": name, "native_type": kind, "source_class": definition["source_class"],
                       "script_pool": pools[kind], "native": native, "source_factories": source_calls,
                       "resource_tuple": metadata["resource_tuple"], "archive": definition["archive"],
                       "bones": metadata["bone_count"], "clips": metadata["clip_count"], "vertices": metadata["vertex_count"],
                       "actor_sha256": metadata["actor_sha256"], "all_parts_sha256": metadata["all_parts_sha256"]})
    scripts = output / "scripts"
    scripts.mkdir(parents=True, exist_ok=True)
    (scripts / "levelB.jpc").write_bytes(data)
    result = {"format": "ED-LEVELB-ACTORS1", "package_sha256": hashlib.sha256(data).hexdigest(), "executable_sha256": EXE_SHA256,
              "startup": {"symbol": ["LevelB", "create", "()V"], "method_index": startup["index"],
                          "asset_names": list(B_STARTUP_ACTORS), "native_pool_capacities": {str(LEVEL_B_ACTORS[name]["native_type"]): LEVEL_B_ACTORS[name]["capacity"] for name in B_STARTUP_ACTORS}},
              "actors": actors,
              "native31": "One native pool31/capacity4/classNetherPet/FSM467BD8. Prototypes31/36/37 retain three exact original resource tuples and shared authored curve IDs.",
              "limits": "Resources, source factories/capacities/FSM and Python pose goldens only. New B gameplay natives/AI are not enabled by this asset export."}
    (output / "levelb-actors.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--profile", choices=("A", "B", "B-startup"), default="A")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    output = args.output or ROOT / ("build/episode/assets" if args.profile == "A" else "build/levelb/assets")
    try:
        results = export_actors(args.game, output, profile=args.profile)
    except (OSError, ValueError, IndexError, KeyError) as error:
        parser.exit(1, f"Error: {error}\n")
    for actor, item in results.items():
        print(f"{actor}: {item['vertex_count']} vertices, {item['bone_count']} bones, {item['clip_count']} clips, {item['actor_bytes']} bytes")


if __name__ == "__main__":
    main()
