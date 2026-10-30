"""Export source dynamic objects for an explicit set of original LevelA scenes."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

from ed_assets import parse_archive
from ed_model import decode_mesh, decode_texture
from ed_scripts import ScriptPackage
from audit_scene_coverage import literal_arguments
from ed_vm import Interpreter
from jpc_scene_activation_case import SCENES, SceneActivationCase


DEGREES = struct.unpack("<f", struct.pack("<f", 0.01745329238474369))[0]


def _f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def _integer(instruction):
    op = instruction["mnemonic"]
    if op.startswith("iconst_"):
        return -1 if op == "iconst_m1" else int(op[7:])
    if op in ("bipush", "sipush"):
        return instruction["operands"][0]
    if op in ("ldc", "ldc_w"):
        return instruction.get("constant", {}).get("signed_value")
    return None


def discover_objects(game, scene_names=SCENES, *, level="A"):
    """Execute actual arrival activation; derive loot IDs from source producers."""
    setups = {}
    if level not in ("A", "B"):
        raise ValueError("Unknown original object resource profile")
    source_calls = {}
    package = ScriptPackage((game / f"data/script/pc/level{level}.jpc").read_bytes())
    if level == "B":
        owners = {"Scene" + name.upper() for name in scene_names}
        for method in package.methods:
            if method["class_name"] not in owners:
                continue
            code = package.disassemble(method["index"])
            for index, row in enumerate(code):
                called = row.get("method", {})
                if called.get("class_name") != "ObjDB" or called.get("name") not in ("create", "createObj"):
                    continue
                args = literal_arguments(code, index, 4)
                if args is None or args[2] != 0:
                    raise ValueError("Unresolved original B object resource tuple")
                item, mesh, unused, texture = args
                key = (item, mesh, texture)
                setups.setdefault(key, set())
                source_calls.setdefault(key, []).append({"scene": method["class_name"][5:].lower(),
                                                         "method": method["index"], "pc": row["offset"]})
    for name in scene_names if level == "A" else ():
        for progression, cord in ((0, 0), (1, 0), (0, 1), (1, 1)):
            case = SceneActivationCase(game)
            case.activate_arrival(name, {405: progression, 435: cord})
            for call in case.engine_calls:
                if call["native"] != "objSetup":
                    continue
                slot, item, mesh, unused, texture = call["args"]
                if unused != 0:
                    raise ValueError("Unverified nonzero PC objSetup fourth argument")
                key = (item, mesh, texture)
                setups.setdefault(key, set()).add((name, slot))
    vm = Interpreter(package)
    loot = {}
    # Each source branch assigns a literal item ID to its result local. This
    # extracts the verified producer outputs; inventory/loot decisions stay JPC.
    for name, signature, store in (("convertClassToPoopableItem", "(LGenericNpc;I)I", "istore_2"),
                                   ("chooseMushroomToPoop", "()I", "istore_1")):
        method = vm.method_index("PoopCommander", name, signature)
        instructions = package.disassemble(method)
        for previous, instruction in zip(instructions, instructions[1:]):
            item = _integer(previous)
            if instruction["mnemonic"] == store and item is not None and item > 0:
                loot.setdefault(item, []).append({"method": method, "pc": previous["offset"]})
    for item, evidence in loot.items():
        mesh, texture = (vm.invoke(vm.method_index("ObjDB", name, "(I)I"), None, (item,))
                         for name in ("getObjectShapeID", "getObjectTextureID"))
        setups.setdefault((item, mesh, texture), set())
    records = [{"item": item, "mesh": mesh, "texture": texture,
                "activation": [{"scene": name, "slot": slot} for name, slot in sorted(contexts)],
                "loot_producer": loot.get(item, []),
                **({"literal_source_calls": source_calls.get((item, mesh, texture), [])} if level == "B" else {})}
               for (item, mesh, texture), contexts in sorted(setups.items())]
    return records


class Resources:
    def __init__(self, game, scene_names=SCENES, *, level="A"):
        self.entries = {}
        self.level = level
        # The modern resolver may use an exact original az00 ID when a shared
        # loot model is absent from az01. Native autoload of az00 is unproven.
        shared = ("az01", "az00") if level == "A" else ("bz01", "az00", "az01")
        for name in (*scene_names, *shared):
            blob = (game / "data" / (name + ".pc")).read_bytes()
            for entry in parse_archive(blob):
                self.entries.setdefault(entry.resource_id, []).append(
                    (name, entry.flags, blob[entry.offset:entry.offset + entry.size]))

    def get(self, identity, expected_flags):
        entries = [entry for entry in self.entries.get(identity, []) if entry[1] == expected_flags]
        rejected = []
        if self.level == "B" and expected_flags == 1:
            complete = []
            for entry in entries:
                try:
                    decode_texture(entry[2])
                except (ValueError, OSError) as error:
                    rejected.append({"archive": entry[0] + ".pc", "bytes": len(entry[2]), "reason": str(error)})
                else:
                    complete.append(entry)
            entries = complete
        if not entries:
            raise ValueError(f"Missing exact original object resource {identity:08x}")
        hashes = {hashlib.sha256(entry[2]).hexdigest() for entry in entries}
        if len(hashes) != 1:
            raise ValueError(f"Conflicting original object resource {identity:08x}")
        name, flags, data = entries[0]
        return data, {"archive": name + ".pc", "resource": identity, "flags": flags,
                      "sha256": next(iter(hashes)), "copies": [e[0] + ".pc" for e in entries],
                      **({"rejected_sources": rejected} if rejected else {})}


def encode_mesh(identity, parts):
    """EDOBJ1: ID/partCount/vertexCount; part flags; <8fIHH> flat triangles."""
    if not 0 < identity <= 0xFFFFFFFF or not 1 <= len(parts) <= 256:
        raise ValueError("Invalid object mesh ID or part count")
    vertices = []
    for index, part in enumerate(parts):
        if any(bone != 65535 for bone in part["bone_indices"]):
            raise ValueError("Object mesh unexpectedly requires skin deformation")
        for triangle in part["triangles"]:
            for vertex in triangle:
                values = (*part["positions"][vertex], *part["normals"][vertex], *part["uv"][vertex])
                if not all(math.isfinite(v) for v in values):
                    raise ValueError("Non-finite source object vertex")
                vertices.append((*values, part["colors"][vertex], index, 0))
    if not 0 < len(vertices) <= 1000000 or len(vertices) % 3:
        raise ValueError("Invalid object triangle count")
    result = bytearray(struct.pack("<8s3I", b"EDOBJ1\0\0", identity, len(parts), len(vertices)))
    result.extend(struct.pack(f"<{len(parts)}I", *(part["flags_raw"] for part in parts)))
    for vertex in vertices:
        result.extend(struct.pack("<8fIHH", *vertex))
    return bytes(result), vertices


def encode_texture(identity, image):
    rgba = image.convert("RGBA")
    width, height = rgba.size
    if not 0 < identity <= 0xFFFFFFFF or not 1 <= width <= 16384 or not 1 <= height <= 16384 or width * height > 16777216:
        raise ValueError("Invalid source object texture dimensions")
    return struct.pack("<8s3I", b"EDTEX1\0\0", identity, width, height) + rgba.tobytes()


def _multiply(a, b):
    return [[_f32(sum(a[row][k] * b[k][col] for k in range(3)))
             for col in range(3)] for row in range(3)]


def object_rotation(degrees):
    """Native423D0F..423D74: row-vector rotation Rz*Ry*Rx."""
    x, y, z = (_f32(angle * DEGREES) for angle in degrees)
    cx, sx, cy, sy, cz, sz = map(_f32, (math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)))
    rx = [[1, 0, 0], [0, cx, sx], [0, -sx, cx]]
    ry = [[cy, 0, -sy], [0, 1, 0], [sy, 0, cy]]
    rz = [[cz, sz, 0], [-sz, cz, 0], [0, 0, 1]]
    return _multiply(rz, _multiply(ry, rx))


def transformed(vertices, position, degrees, floor_height, flags):
    rotation = object_rotation(degrees)
    origin = list(position)
    if flags & 2:
        origin[1] = _f32(origin[1] + floor_height)
    result = []
    for vertex in vertices:
        point = [_f32(sum(vertex[k] * rotation[k][col] for k in range(3)) + origin[col]) for col in range(3)]
        normal = list(rotation[1])  # Native423b23..35 resets every source normal to(0,1,0).
        result.append((*point, *normal, *vertex[6:8], 0xFFFFFFFF, *vertex[9:]))
    return result


def export_runtime_objects(game, output, scene_names=SCENES, *, level="A"):
    game, output = Path(game), Path(output)
    records, resources = discover_objects(game, scene_names, level=level), Resources(game, scene_names, level=level)
    target = output / "objects"
    (target / "models").mkdir(parents=True, exist_ok=True)
    (target / "textures").mkdir(parents=True, exist_ok=True)
    models, textures, golden = [], [], []
    for identity in sorted({record["mesh"] for record in records}):
        data, source = resources.get(identity, 0)
        parts = decode_mesh(data)
        encoded, vertices = encode_mesh(identity, parts)
        (target / "models" / f"{identity:08x}.bin").write_bytes(encoded)
        models.append({**source, "part_count": len(parts), "part_flags": [part["flags_raw"] for part in parts],
                       "visible_parts": list(range(len(parts))), "vertices": len(vertices),
                       "bounds": [[min(v[axis] for v in vertices) for axis in range(3)],
                                  [max(v[axis] for v in vertices) for axis in range(3)]]})
        for degrees in ((0, 0, 0), (0, 90, 0), (30, 60, 90)):
            position, floor_height, flags = (2.0, .5, -1.0), 1.25, 3
            golden.append((identity, position, degrees, floor_height, flags,
                           transformed(vertices, position, degrees, floor_height, flags)))
    for identity in sorted({record["texture"] for record in records}):
        data, source = resources.get(identity, 1)
        image, name = decode_texture(data)
        (target / "textures" / f"{identity:08x}.rgba").write_bytes(encode_texture(identity, image))
        image.save(target / "textures" / f"{identity:08x}.bmp")
        textures.append({**source, "source_name": name, "width": image.width, "height": image.height,
                         "alpha_range": list(image.getchannel("A").getextrema())})
    blob = bytearray(struct.pack("<8sI", b"EDOBPOSE", len(golden)))
    for identity, position, degrees, height, flags, vertices in golden:
        blob.extend(struct.pack("<I3f3if2I", identity, *position, *degrees, height, flags, len(vertices)))
        for vertex in vertices:
            blob.extend(struct.pack("<8fIHH", *vertex))
    (target / "pose-goldens.bin").write_bytes(blob)
    manifest = {"scenes": list(scene_names), "setup": "(index,itemType,meshResource,unusedPCArg0,textureResource)",
                "models": models, "textures": textures, "objects": records,
                "geometry": "Original rigid object meshes; sentinel65535 assignments, all source parts preserved. Raw colors/normals retained; native423a70 initializes rendered colorwhite and normalsup.",
                "transforms": "Native423C80: flags2 adds BV floor to localY; source scale1 and Rz*Ry*Rx EulerXYZ degrees.",
                "resource_policy": "Exact original IDs only; az00 is allowed for shared loot models absent az01. Native PC az00 autoload is unproven; no resource aliasing.",
                "limits": "Only selected-scene activation and actual PoopCommander897/879 item resources. Other levels and unknown dynamic resources remain strict missing-resource failures."}
    (target / "source.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest = export_runtime_objects(args.game, args.output)
    print(f"Original objects: {len(manifest['models'])} meshes, {len(manifest['textures'])} textures")


if __name__ == "__main__":
    main()
