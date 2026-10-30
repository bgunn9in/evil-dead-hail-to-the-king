"""Audit original A/B scene registries and asset coverage without exporting assets."""

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct

from ed_assets import parse_archive
from ed_audio import sound_bundle
from ed_camera import parse_scene
from ed_collision import parse_collision
from ed_masks import mask_clip_depth, read_depth_patches
from ed_movies import movie_id, movie_name
from ed_portals import scene_name
from ed_scripts import ScriptPackage, _integer
from ed_triggers import parse_triggers
from ed_vm import Interpreter

CURRENT = frozenset(("ak01", "ak09", "al01", "al03", "ah50", "ah51", "ah01", "ah02"))
NEAREST = ("ab50", "al50", "ac51", "al02", "ah05")


def registry(package, level):
    """Validate the exact createScene switch case -> new SceneXX mapping."""
    matches = [m for m in package.methods if m["class_name"] == level and m["name"] == "createScene"]
    if len(matches) != 1:
        raise ValueError("Expected one level scene factory")
    method = matches[0]
    code = package.disassemble(method["index"])
    switches = [i for i in code if i["mnemonic"] == "lookupswitch"]
    if len(switches) != 1:
        raise ValueError("Unsupported level scene factory switch")
    offsets = {i["offset"]: n for n, i in enumerate(code)}
    result = []
    for identity, target in switches[0]["cases"]:
        branch = code[offsets[target]:offsets[target] + 7]
        created = [i for i in branch if i["mnemonic"] == "new"]
        if len(created) != 1 or created[0]["class_name"] != "Scene" + scene_name(identity).upper():
            raise ValueError("Scene factory identity/class mismatch")
        result.append({"scene": scene_name(identity), "scene_id": identity,
                       "class": created[0]["class_name"], "factory_method": method["index"],
                       "factory_pc": created[0]["offset"]})
    if len({r["scene"] for r in result}) != len(result):
        raise ValueError("Duplicate scene factory class")
    return result


def literal_arguments(code, position, count):
    """Return only adjacent literal arguments; never infer values across flow."""
    if position < count:
        return None
    try:
        return [_integer(i) for i in code[position - count:position]]
    except ValueError:
        return None


def movie_literals(literals):
    result = []
    for identity in sorted(literals):
        try:
            movie_name(identity)
            result.append(identity)
        except ValueError:
            pass
    return result


def background_structure(payload, identity, index, patches):
    if len(payload) < 128 or b"\0" not in payload[:128]:
        raise ValueError("Missing background name")
    name = payload[:128].split(b"\0", 1)[0].decode("ascii")
    cursor = 128
    for _ in range(15):
        if cursor + 4 > len(payload):
            raise ValueError("Truncated JPEG length")
        size = struct.unpack_from("<I", payload, cursor)[0]
        cursor += 4
        if size < 4 or size % 4 or size > len(payload) - cursor or payload[cursor:cursor + 2] != b"\xff\xd8":
            raise ValueError("Invalid JPEG tile allocation")
        cursor += size
    if cursor + 4 > len(payload):
        raise ValueError("Missing foreground count")
    count = struct.unpack_from("<I", payload, cursor)[0]
    table = cursor + 4
    if count > (len(payload) - table) // 364:
        raise ValueError("Foreground table exceeds background")
    depths, skipped = [], []
    for layer in range(count):
        offset = table + layer * 364
        distance = struct.unpack_from("<f", payload, offset)[0]
        try:
            depth = mask_clip_depth(distance, scene_id=identity, background_index=index,
                                    layer_index=layer, depth_patches=patches)
            depths.append(depth if math.isfinite(depth) else None)
            if not math.isfinite(depth):
                skipped.append({"layer": layer, "distance": distance, "native_depth": "positive_infinity",
                                "skip_reason": "positive_infinity_has_no_finite_occlusion"})
        except ValueError as error:
            raise ValueError(f"Background{index} {name}, layer{layer}, rawdistance={distance}: {error}") from error
        for tile in range(15):
            mask = struct.unpack_from("<I", payload, offset + 20 + tile * 24)[0]
            if mask != 0xffffffff and (mask < table + count * 364 or mask + 2048 > len(payload)):
                raise ValueError("Foreground bitmask exceeds background")
    return {"source_name": name, "layers": count, "clip_depths": depths, "nonfinite_layers": skipped}


def raw_portals(payload):
    count, offset = struct.unpack_from("<2I", payload, 40)
    if offset > len(payload) or count > (len(payload) - offset) // 72 or (count and offset < 88):
        raise ValueError("Portal table exceeds scene")
    result = []
    for index in range(count):
        start = offset + index * 72
        mode, shape, camera, marker = struct.unpack_from("<4B", payload, start)
        destination = struct.unpack_from("<I", payload, start + 56)[0]
        record = {"index": index, "mode": mode, "shape": shape, "destination_id": destination,
                  "destination": scene_name(destination), "camera": camera, "marker": marker,
                  "source_offset": start}
        if shape in (0, 2):
            record["points_xyz"] = [list(p) for p in struct.iter_unpack("<3f", payload[start + 4:start + 4 + (2 if shape == 0 else 4) * 12])]
        record["rotation_xyz"] = list(struct.unpack_from("<3f", payload, start + 60))
        result.append(record)
    return result


def audit(game):
    game = Path(game)
    resources, archives, sound_index, scene_payloads = {}, {}, {}, {}
    for path in sorted((game / "data").glob("*.pc")):
        data = path.read_bytes()
        entries = parse_archive(data)
        archives[path.stem] = {"path": "data/" + path.name, "sha256": hashlib.sha256(data).hexdigest(),
                               "bytes": len(data), "entries": len(entries),
                               "types": dict(sorted(Counter(f"{e.resource_id >> 24:02x}:{e.flags}" for e in entries).items()))}
        for e in entries:
            raw = data[e.offset:e.offset + e.size]
            record = {"archive": path.stem, "entry": e.index, "flags": e.flags, "offset": e.offset,
                      "bytes": e.size, "sha256": hashlib.sha256(raw).hexdigest()}
            resources.setdefault(e.resource_id, []).append(record)
            if e.resource_id >> 24 == 15:
                if path.stem in scene_payloads:
                    raise ValueError("Multiple native scenes in one archive")
                scene_payloads[path.stem] = (struct.unpack_from("<I", data)[0], raw, record)
            if e.resource_id >> 24 == 14 and e.flags == 1:
                for sound in sound_bundle(raw):
                    sound_index.setdefault(sound["id"], []).append({"archive": path.stem, "entry": e.index,
                        "sound_index": sound["index"], "format": sound.get("unsupported", "PCM"),
                        **{k: sound[k] for k in ("channels", "sample_rate", "bits", "frames") if k in sound}})
    movies = {}
    for path in sorted((game / "fmv").glob("*.bik")):
        try:
            identity = movie_id(path.stem)
        except ValueError:
            continue
        with path.open("rb") as source:
            head = source.read(44)
        fields = struct.unpack("<4s10I", head)
        movies[identity] = {"path": "fmv/" + path.name, "tag": fields[0].decode("ascii"),
                           "bytes": path.stat().st_size, "declared_bytes_match": fields[1] + 8 == path.stat().st_size,
                           "frames": fields[2], "width": fields[5], "height": fields[6],
                           "fps": [fields[7], fields[8]], "validation": "44-byte header only; no decoder run"}
    patches = read_depth_patches((game / "EvilDead.exe").read_bytes())
    packages, scenes, actor_factories, actor_classes, actor_types, object_catalogs = {}, {}, {}, {}, {}, {}
    for level in ("LevelA", "LevelB"):
        path = game / f"data/script/pc/level{level[-1]}.jpc"
        package = ScriptPackage(path.read_bytes())
        packages[level] = {"path": str(path.relative_to(game)).replace("\\", "/"),
                           "sha256": hashlib.sha256(package.data).hexdigest(),
                           "classes": len(package.classes), "methods": len(package.methods)}
        disassembly = {m["index"]: package.disassemble(m["index"]) for m in package.methods}
        package_literals = {i["constant"]["value_raw"] for code in disassembly.values() for i in code
                            if i["mnemonic"] in ("ldc", "ldc_w") and i["constant"]["index_raw"] >= 0}
        packages[level]["declared_movie_literals"] = movie_literals(package_literals)
        packages[level]["missing_declared_movies"] = [i for i in movie_literals(package_literals) if i not in movies]
        factories = []
        for method in package.methods:
            for position, ins in enumerate(disassembly[method["index"]]):
                target = ins.get("method", {})
                if target.get("class_name") == "ActorDB" and target["name"].startswith("create") and target["descriptor"].startswith("(III)"):
                    factories.append({"caller_class": method["class_name"], "method": method["index"], "pc": ins["offset"],
                        "factory": target["name"], "resource_tuple": literal_arguments(disassembly[method["index"]], position, 3)})
                if target.get("class_name") == "ActorDB" and target["name"] == "create" and target["descriptor"] == "(IIII)LGenericNpc;":
                    args = literal_arguments(disassembly[method["index"]], position, 4)
                    if args is not None:
                        factories.append({"caller_class": method["class_name"], "method": method["index"], "pc": ins["offset"],
                            "factory": "create", "native_type": args[0], "resource_tuple": args[1:]})
        actor_factories[level] = factories
        actor_create = next(m for m in package.methods if m["class_name"] == "ActorDB" and m["name"] == "create" and m["descriptor"] == "(IIII)LGenericNpc;")
        actor_code = disassembly[actor_create["index"]]
        actor_offsets = {i["offset"]: n for n, i in enumerate(actor_code)}
        actor_switch = next(i for i in actor_code if i["mnemonic"] == "lookupswitch")
        actor_classes[level] = {key: {"class": actor_code[actor_offsets[target]]["class_name"],
            "factory_method": actor_create["index"], "factory_pc": target,
            "source_implemented": actor_code[actor_offsets[target]]["class_name"] != "java/lang/Object"}
            for key, target in actor_switch["cases"]}
        type_method = next(m for m in package.methods if m["class_name"] == "ActorDB" and m["name"] == "createType")
        type_code = disassembly[type_method["index"]]
        type_offsets = {i["offset"]: n for n, i in enumerate(type_code)}
        type_switch = next(i for i in type_code if i["mnemonic"] == "lookupswitch")
        actor_types[level] = {}
        for key, target in type_switch["cases"]:
            position = next(n for n in range(type_offsets[target], len(type_code))
                            if type_code[n].get("method", {}).get("class_name") == "ActorDB")
            call = type_code[position]
            factory = call["method"]["name"]
            count = 4 if factory == "create" else 3
            args = literal_arguments(type_code, position, count)
            if args is None:
                raise ValueError("Actor resource prototype is not adjacent source literals")
            native_type = args[0] if count == 4 else {"createAsh": 0, "createDeadite": 4, "createSkullBat": 2}[factory]
            actor_types[level][key] = {"method": type_method["index"], "branch_pc": target,
                "call_pc": call["offset"], "native_type": native_type, "resource_tuple": args[-3:],
                "actor_class": actor_classes[level].get(native_type),
                "resource_locations": [sorted({r["archive"] for r in resources.get(i, []) if r["flags"] == (1 if n == 2 else 0)})
                                       for n, i in enumerate(args[-3:])]}
        vm = Interpreter(package)
        getters = {m["name"]: m["index"] for m in package.methods if m["class_name"] == "ObjDB" and m["descriptor"] == "(I)I"}
        items = sorted({key for mid in getters.values() for i in disassembly[mid] for key, _ in i.get("cases", []) if key > 0})
        catalog = {}
        for item in items:
            metadata = {name: vm.invoke(getters[name], None, (item,)) for name in
                        ("getObjectShapeID", "getObjectTextureID", "getObjectGuiGeomID", "getObjectGuiTextureID", "getObjectGuiShapeID", "getObjectFlags")}
            metadata["resource_locations"] = {key: sorted({e["archive"] for e in resources.get(metadata[key], []) if e["flags"] == flag})
                for key, flag in (("getObjectShapeID", 0), ("getObjectTextureID", 1), ("getObjectGuiGeomID", 0), ("getObjectGuiTextureID", 1))}
            catalog[item] = metadata
        object_catalogs[level] = catalog
        for entry in registry(package, level):
            name, identity = entry["scene"], entry["scene_id"]
            own = [m for m in package.methods if m["class_name"] == entry["class"]]
            literals, native, actor_calls, object_calls, spawn_calls = set(), set(), set(), [], []
            for method in own:
                code = disassembly[method["index"]]
                for position, ins in enumerate(code):
                    if ins["mnemonic"] in ("ldc", "ldc_w") and ins["constant"]["index_raw"] >= 0:
                        literals.add(ins["constant"]["value_raw"])
                    target = ins.get("method", {})
                    if target.get("class_name") == "ed/base/External":
                        native.add(target["name"] + target["descriptor"])
                    if target.get("class_name") == "ActorDB" or target.get("class_name") in ("SceneA", "SceneB") and target.get("name") in ("deadite", "skullbat"):
                        actor_calls.add(target["class_name"] + "." + target["name"] + target["descriptor"])
                    if target.get("class_name") == "ObjDB" and target.get("name") in ("create", "createObj"):
                        args = literal_arguments(code, position, 4)
                        call = {"method": method["index"], "pc": ins["offset"], "literal_args": args}
                        if args is not None:
                            call["mesh_archives"] = sorted({r["archive"] for r in resources.get(args[1], []) if r["flags"] == 0})
                            call["texture_archives"] = sorted({r["archive"] for r in resources.get(args[3], []) if r["flags"] == 1})
                        object_calls.append(call)
                    if target.get("class_name") == "SpawnCommander" and target.get("name") == "spawnSetSpawnObjectType":
                        args = literal_arguments(code, position, 2)
                        tail = literal_arguments(code, position, 1)
                        actor_type = tail[0] if tail is not None else None
                        spawn_calls.append({"method": method["index"], "pc": ins["offset"], "literal_args": args,
                            "actor_type_literal": actor_type,
                            "actor_class": actor_types[level].get(actor_type, {}).get("actor_class"),
                            "resource_prototype": actor_types[level].get(actor_type)})
            record = {**entry, "level": level, "currently_exported": name in CURRENT,
                      "method_hashes": [{"index": m["index"], "name": m["name"], "file_offset": 56 + m["code_offset"],
                          "sha256": hashlib.sha256(package.data[56 + m["code_offset"]:56 + m["code_offset"] + m["code_length"]]).hexdigest()} for m in own if m["has_code"]],
                      "direct_resource_literals": sorted(literals & resources.keys()),
                      "direct_sound_literals": sorted(literals & sound_index.keys()),
                      "direct_movie_literals": movie_literals(literals),
                      "missing_direct_movies": [i for i in movie_literals(literals) if i not in movies],
                      "direct_native_calls": sorted(native), "direct_actor_calls": sorted(actor_calls),
                      "literal_object_create_calls": object_calls, "spawn_type_calls": spawn_calls, "parsers": {}}
            scenes[name] = record
            if name not in scene_payloads:
                record["missing_scene_archive"] = True
                continue
            archive_id, payload, source = scene_payloads[name]
            record["source"] = source
            if archive_id != identity:
                raise ValueError("Archive and JPC scene identities differ")
            camera = None
            for parser_name, parser in (("camera", parse_scene), ("collision", parse_collision), ("triggers", parse_triggers)):
                try:
                    parsed = parser(payload, camera_events=True) if parser_name == "triggers" else parser(payload)
                    record["parsers"][parser_name] = {"supported": True}
                    if parser_name == "camera":
                        camera = parsed
                        record["camera_count"] = len(parsed["tracks"])
                        record["marker_count"] = len(parsed["markers"])
                        record["animated_tracks"] = [t["index"] for t in parsed["tracks"] if len(t["frames"]) > 1]
                    elif parser_name == "collision":
                        record["floor_triangles"] = len(parsed["floor"]["triangles"])
                        record["solids"] = len(parsed["solids"])
                    elif parser_name == "triggers":
                        record["parsers"][parser_name]["requires_camera_events"] = parsed["requires_camera_events"]
                except (ValueError, KeyError, IndexError, struct.error) as error:
                    record["parsers"][parser_name] = {"supported": False, "error": str(error)}
            count, offset = struct.unpack_from("<2I", payload, 8)
            record["volume_shapes"] = dict(sorted(Counter(f"{payload[offset + i*72]}:{payload[offset + i*72+1]}" for i in range(count)).items()))
            record["portals"] = raw_portals(payload)
            if camera:
                path = game / f"data/{name}v.pc"
                try:
                    background = path.read_bytes()
                    entries = [e for e in parse_archive(background) if e.resource_id >> 24 == 8 and e.flags == 0]
                    indices = sorted({f["background_index"] for t in camera["tracks"] for f in t["frames"]})
                    record["backgrounds"] = [{"index": i, "entry": entries[i].index,
                        **background_structure(background[entries[i].offset:entries[i].offset + entries[i].size], identity, i, patches)} for i in indices]
                    record["parsers"]["background_masks"] = {"supported": True, "frame_count": len(indices)}
                except (OSError, ValueError, IndexError, struct.error) as error:
                    record["parsers"]["background_masks"] = {"supported": False, "error": str(error)}
    frontier = []
    for source in scenes.values():
        for portal in source.get("portals", []):
            target = scenes.get(portal["destination"])
            if portal["mode"] == 0:
                portal["arrival_resolved"] = bool(target and "marker_count" in target and portal["marker"] < target["marker_count"] and portal["camera"] < target["camera_count"])
            if source["scene"] in CURRENT and portal["mode"] == 0 and portal["destination"] not in CURRENT:
                frontier.append({"scene": source["scene"], **portal})
    return {"schema_version": 1, "method": "Original JPC factory switches plus checked PC tables; conservative literal dependencies, no VM gameplay branches executed.",
            "limits": ["Literal resource/sound/movie references include unexecuted branches and omit computed IDs; they are not an activation result.",
                       "Literal object arguments require adjacent constants. Null means unresolved, never a fabricated item.",
                       "A parser success is format coverage, not full gameplay/runtime support. Native activation/actor actions remain strict.",
                       "Camera-event conditions use an explicit parser option and EDTRIG3 rules; they are never flattened into unconditional enter actions.",
                       "Resource location proves original bytes exist, not native autoload of another archive.",
                       "Background JPEG allocations and masks checked structurally; JPEG pixels not decoded; current base exporter exports camera frame0 only."],
            "summary": {"registered_scenes": len(scenes), "per_level": dict(Counter(s["level"] for s in scenes.values())),
                        "archive_count": len(archives), "resource_ids": len(resources), "movie_files": len(movies),
                        "sound_ids": len(sound_index), "current_frontier": sorted({p["destination"] for p in frontier}),
                        "parser_coverage": {kind: sum(s["parsers"].get(kind, {}).get("supported", False) for s in scenes.values())
                            for kind in ("camera", "collision", "triggers", "background_masks")},
                        "unresolved_mode0_arrivals": sum(p["mode"] == 0 and not p["arrival_resolved"] for s in scenes.values() for p in s.get("portals", []))},
            "packages": packages, "archives": archives, "scenes": scenes, "frontier": frontier,
            "priority_scenes": {name: scenes.get(name) for name in NEAREST},
            "actor_factories": actor_factories, "actor_classes": actor_classes, "actor_types": actor_types, "object_catalogs": object_catalogs,
            "resources": {f"{identity:08x}": copies for identity, copies in sorted(resources.items())},
            "sounds": {f"{identity:08x}": copies for identity, copies in sorted(sound_index.items())},
            "movies": {f"{identity:08x}": movie for identity, movie in sorted(movies.items())},
            "unregistered_scene_archives": sorted(set(scene_payloads) - set(scenes))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=Path(__file__).resolve().parents[1] / "game")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = audit(args.game)
    if args.output:
        game, output = args.game.resolve(), args.output.resolve()
        if output == game or game in output.parents:
            raise ValueError("Audit output must be outside original game directory")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result["summary"], indent=2))


if __name__ == "__main__":
    main()
