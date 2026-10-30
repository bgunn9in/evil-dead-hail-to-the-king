"""Export verified LevelA rooms supported by the current original actor registry."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import struct

from ed_assets import parse_archive
from ed_audio import export_runtime_audio
from ed_camera import parse_scene
from ed_collision import parse_collision
from ed_masks import read_depth_patches
from ed_portals import parse_portals, resolve_transition
import export_scene as base
from ed_movies import export_runtime_movies
from ed_movies import movie_name
from ed_objects import export_runtime_objects
from ed_vfx import export_runtime_vfx
from jpc_scene_activation_case import run_case as activation_audit
from jpc_levela_scene_probe import run_probe
from ed_scripts import ScriptPackage
from audit_scene_coverage import registry, literal_arguments
from jpc_at01_spawn_case import run_case as at01_inactive_proof


LEGACY_SCENES = ("ak01", "ak09", "al01", "al03", "ah50", "ah51", "ah01", "ah02", "ab50", "ac51", "al50")
SCENES = (*LEGACY_SCENES, "ab01", "ac01", "ac02", "ac03", "ac04", "ac14", "ac52", "ag01", "ag02",
          "ah05", "ah10", "ah12", "ah20", "al02", "at01", "at02", "au01", "ac05", "ag03", "ah06", "au04")
ACTOR_KEYS = ("source_game", "mesh", "body_parts", "texture", "vertex_count", "idle_resource_id",
              "idle_frames", "walk_resource_id", "walk_frames", "animation_fps", "animation_fps_status", "root_motion")


def levela_coverage(game):
    """Check source factories against explicit compiled registry capabilities."""
    header = (base.ROOT / "runtime/gameplay_actors.h").read_text(encoding="utf-8")
    match = re.search(r"SupportedTypes\s*\{\{([0-9, ]+)\}\}", header)
    if not match and (base.ROOT / "runtime/actor_catalog.h").is_file():
        catalog = (base.ROOT / "runtime/actor_catalog.h").read_text(encoding="utf-8")
        header += "\n" + catalog
        match = re.search(r"SupportedTypes\s*\{\{([0-9, ]+)\}\}", catalog)
    if not match:
        raise ValueError("Cannot read the explicit native actor capability table")
    supported = {int(value) for value in match[1].split(",")}
    package = ScriptPackage((game / "data/script/pc/levelA.jpc").read_bytes())
    result = []
    for item in registry(package, "LevelA"):
        calls = []
        for method in package.methods:
            if method["class_name"] != item["class"]:
                continue
            code = package.disassemble(method["index"])
            for index, row in enumerate(code):
                target = row.get("method", {})
                if (target.get("class_name"), target.get("name")) != ("SpawnCommander", "spawnSetSpawnObjectType"):
                    continue
                args = literal_arguments(code, index, 1)
                calls.append({"method": method["index"], "pc": row["offset"],
                              "prototype": args[0] if args is not None else None})
        types = {call["prototype"] for call in calls}
        unsupported = types - supported
        notes = []
        inactive_evidence = None
        # Verified by original fresh/save/restore/932 proof; no generic actor16 alias.
        if item["scene"] == "at01" and unsupported == {16}:
            proof = at01_inactive_proof(game)
            if (proof["source_script_sha256"] != hashlib.sha256(package.data).hexdigest()
                    or proof["fresh_wave3_flags"] & 0x10000 or proof["restored_wave3_flags"] & 0x10000
                    or proof["isolated_in_progress"]):
                raise ValueError("AT01's source-inactive actor16 exception is no longer proven")
            inactive_evidence = {key: proof[key] for key in ("source_script_sha256", "fresh_wave3_flags", "restored_wave3_flags", "isolated_source_ticks")}
            unsupported = set()
            notes.append("Native16 wave3 config is source-inactive; see at01-spawn16-format.json. An actual creation remains strict unsupported.")
        if item["scene"] == "aa01":
            included, reason = False, "Menu scene: no original arrival marker or playable spawn; registry retained only."
        elif unsupported:
            included, reason = False, "Source actor prototype not supported by the compiled actor registry."
        else:
            included, reason = True, "Source scene actor requirements fit the current bounded actor registry."
        result.append({**item, "spawn_calls": calls, "actor_prototypes": sorted(types, key=lambda value: -1 if value is None else value),
                       "unsupported_actor_prototypes": sorted(unsupported, key=lambda value: -1 if value is None else value),
                       "included": included, "reason": reason, "notes": notes,
                       "bounded_inactive_evidence": inactive_evidence})
    selected = {row["scene"] for row in result if row["included"]}
    if selected != set(SCENES):
        raise ValueError(f"Export scene list differs from verified source/capabilities: add={sorted(selected-set(SCENES))}, remove={sorted(set(SCENES)-selected)}")
    return {"supported_native_types": sorted(supported), "registry_header_sha256": hashlib.sha256(header.encode()).hexdigest(),
            "source_script_sha256": hashlib.sha256(package.data).hexdigest(), "scenes": result}


def source_dependencies(game, scenes, *, level="A"):
    if level not in ("A", "B"):
        raise ValueError("Unknown original dependency profile")
    package = ScriptPackage((game / f"data/script/pc/level{level}.jpc").read_bytes())
    movies, switches = {}, []
    selected = {"Scene" + name.upper() for name in scenes}
    for method in package.methods:
        if method["class_name"].startswith("Scene") and method["class_name"] not in selected and method["class_name"] not in ("SceneA", "SceneB", "SceneCommander"):
            continue
        code = package.disassemble(method["index"])
        for index, row in enumerate(code):
            if row["mnemonic"].startswith("ldc"):
                value = row["constant"]["value_raw"]
                try:
                    name = movie_name(value)
                except ValueError:
                    continue
                if not (game / "fmv" / (name + ".bik")).is_file():
                    raise ValueError(f"Source movie dependency is missing: {name}")
                movies.setdefault(value, []).append({"method": method["index"], "pc": row["offset"],
                                                    "owner": method["class_name"], "method_name": method["name"]})
            target = row.get("method", {})
            if (target.get("class_name"), target.get("name"), target.get("descriptor")) == ("ed/base/External", "sceneSwitch", "(IIII)V"):
                args = literal_arguments(code, index, 4)
                if args is not None:
                    switches.append({"method": method["index"], "pc": row["offset"], "owner": method["class_name"],
                                     "destination": args[0], "camera": args[1], "marker": args[2], "degrees": args[3]})
    return {"movies": [{"id": identity, "source_name": movie_name(identity) + ".bik", "literal_references": refs}
                        for identity, refs in sorted(movies.items())], "literal_scene_switches": switches,
            "limits": "Static movie literals are resource dependencies, not a claim that every movie plays on every route. Original JPC calls decide playback."}


def portal_manifest(parsed, destinations):
    """Preserve all transition volumes; unavailable destinations have no pose."""
    records = []
    for portal in parsed["portals"]:
        if portal["mode"] != 0:
            continue
        target = destinations.get(portal["destination_scene_id"])
        transition = resolve_transition(portal, target) if target is not None else None
        records.append({**portal, "transition": transition,
                        "destination_available": transition is not None})
    return {"scene_name": parsed["scene_name"], "scene_id": parsed["scene_id"],
            "spatial_grid": parsed["spatial_grid"], "portals": records}


def pack_portals(manifest):
    """EDPOR1: source IDs/geometry and optional resolved original marker XYZ.

    Header <8s4sI4f>: magic, scene, count, grid originXZ and extentXZ.
    Record <7I4s>: index, shape, pointCount, mask, destinationID, camera,
    marker, destinationASCII; then pointCount*<2f> XZ, <3f> EulerXYZ,
    u32 resolved. Resolved records append <3f> destination markerXYZ.
    Native arrival discards markerY and resumes on the destination BV floor.
    """
    name, records, grid = manifest["scene_name"], manifest["portals"], manifest["spatial_grid"]
    if len(name) != 4 or len(records) > 1000:
        raise ValueError("Invalid portal scene name or count")
    values = [*grid["grid_origin_xz"], *grid["grid_extent_xz"]]
    if len(values) != 4 or not all(math.isfinite(v) for v in values) or min(values[2:]) <= 0:
        raise ValueError("Invalid portal spatial grid")
    data = bytearray(struct.pack("<8s4sI4f", b"EDPOR1\0\0", name.encode("ascii"), len(records), *values))
    seen = set()
    for portal in records:
        shape, points = portal["shape"], portal["points"]
        if (portal["index"] in seen or portal["mode"] != 0 or shape not in (0, 2)
                or len(points) != (2 if shape == 0 else 4) or len(portal["destination_scene"]) != 4):
            raise ValueError("Invalid source transition identity or geometry")
        seen.add(portal["index"])
        rotation = portal["rotation_xyz"]
        if len(rotation) != 3 or not all(math.isfinite(v) for p in points + [rotation] for v in p):
            raise ValueError("Non-finite portal geometry or rotation")
        data.extend(struct.pack("<7I4s", portal["index"], shape, len(points), portal["spatial_mask"],
                                portal["destination_scene_id"], portal["camera_index"], portal["marker_index"],
                                portal["destination_scene"].encode("ascii")))
        for x, _, z in points:
            data.extend(struct.pack("<2f", x, z))
        data.extend(struct.pack("<3fI", *rotation, int(portal["destination_available"])))
        if portal["destination_available"]:
            target = portal["transition"]
            if target is None or target["scene_id"] != portal["destination_scene_id"]:
                raise ValueError("Resolved portal target identity does not match")
            marker = target["marker_xyz"]
            if len(marker) != 3 or not all(math.isfinite(v) for v in marker):
                raise ValueError("Invalid resolved arrival marker")
            data.extend(struct.pack("<3f", *marker))
        elif portal["transition"] is not None:
            raise ValueError("Unavailable portal must not contain an arrival pose")
    return bytes(data)


def _edge_distance(x, z, a, b):
    dx, dz = b[0] - a[0], b[1] - a[1]
    squared = dx * dx + dz * dz
    fraction = max(0, min(1, ((x - a[0]) * dx + (z - a[1]) * dz) / squared)) if squared else 0
    return math.hypot(x - a[0] - dx * fraction, z - a[1] - dz * fraction)


def spawn_clearance(collision, x, z, *, floor_edges=None):
    """Measure existing modern actor clearance, without moving source markers."""
    if floor_edges is None:
        floor_edges = [(a[::2], b[::2]) for a, b in base.floor_boundary_edges(collision["floor"])]
    distances = [_edge_distance(x, z, a, b) for a, b in floor_edges]
    for solid in collision["solids"]:
        if solid["shape"] == 3:
            circle = solid["circle_xz"]
            center = circle["center"]
            distances.append(math.hypot(x - center[0], z - center[1]) - circle["radius"])
            continue
        points = solid["vertices_xz"]
        edges = list(zip(points, points[1:] + points[:1]))
        if all((b[0] - a[0]) * (z - a[1]) - (b[1] - a[1]) * (x - a[0]) <= 1e-7 for a, b in edges):
            return 0.0
        distances.extend(_edge_distance(x, z, a, b) for a, b in edges)
    return min(distances, default=math.inf)


def arrival_spawn(name, contexts, destinations, scene_switches=()):
    context = contexts[name]
    authored = []
    for parsed in destinations.values():
        for portal in parsed["portals"]:
            if portal["mode"] != 0 or portal["destination_scene_id"] != context["id"]:
                continue
            target = resolve_transition(portal, context["portals"])
            x, _, z = target["spawn_xyz"]
            try:
                height = base.source_floor_height(context["collision"]["floor"], x, z)
            except ValueError:
                continue
            clearance = spawn_clearance(context["collision"], x, z)
            role = {"role": "original in-episode portal arrival marker" if parsed["scene_name"] in contexts else "original source portal arrival marker (origin not exported)",
                    "source_scene": parsed["scene_name"], "source_scene_available": parsed["scene_name"] in contexts,
                    "source_portal": portal["index"], "marker_index": target["marker_index"], "camera_index": target["camera_index"],
                    "yaw": target["yaw"], "static_clearance": clearance, "modern_clearance_satisfied": clearance + 1e-6 >= base.PLAYER_RADIUS}
            if clearance > 0:
                authored.append(([x, height, z], role))
            if clearance + 1e-6 >= base.PLAYER_RADIUS:
                return [x, height, z], role
    for switch in scene_switches:
        if switch["destination"] != context["id"]:
            continue
        marker, camera = switch["marker"], switch["camera"]
        if not 0 <= marker < len(context["scene"]["markers"]) or not 0 <= camera < len(context["scene"]["tracks"]):
            raise ValueError("Original literal sceneSwitch has an invalid destination marker or camera")
        x, _, z = context["scene"]["markers"][marker]
        try:
            height = base.source_floor_height(context["collision"]["floor"], x, z)
        except ValueError:
            continue
        clearance = spawn_clearance(context["collision"], x, z)
        if clearance + 1e-6 >= base.PLAYER_RADIUS:
            radians = struct.unpack("<f", struct.pack("<f", switch["degrees"] * 0.01745329238474369))[0]
            return [x, height, z], {"role": "original literal JPC sceneSwitch arrival marker",
                                   "source_method": switch["method"], "source_pc": switch["pc"],
                                   "marker_index": marker, "camera_index": camera, "yaw": radians,
                                   "source_degrees": switch["degrees"], "static_clearance": clearance}
    if authored:
        position, role = max(authored, key=lambda candidate: candidate[1]["static_clearance"])
        role["clearance_note"] = "Exact original marker retained; it is closer to a raw floor boundary than modern actor radius. No authored position was shifted."
        return position, role
    raise ValueError(f"No unchanged original arrival marker has a valid floor and lies outside source solids: {name}")


def load_contexts(game, scenes=SCENES):
    contexts, destinations = {}, {}
    for name in scenes:
        blob = (game / f"data/{name}.pc").read_bytes()
        entries = [entry for entry in parse_archive(blob) if entry.resource_id >> 24 == 15]
        if len(entries) != 1:
            raise ValueError("Expected one original scene resource")
        entry = entries[0]
        payload = blob[entry.offset:entry.offset + entry.size]
        identity = struct.unpack_from("<I", blob)[0]
        parsed = parse_portals(payload, identity)
        if parsed["scene_name"] != name or identity in destinations:
            raise ValueError("Source scene identity mismatch")
        contexts[name] = {"id": identity, "payload": payload, "scene": parse_scene(payload),
                          "collision": parse_collision(payload), "portals": parsed}
        destinations[identity] = parsed
    return contexts, destinations


def export_episode(game, output, *, profile="A"):
    if profile == "B":
        return export_levelb_episode(game, output)
    if profile != "A":
        raise ValueError("Unknown explicit episode resource profile")
    game, output = Path(game).resolve(), Path(output).resolve()
    if output == game or game in output.parents:
        raise ValueError("Generated assets must be outside the original game directory")
    coverage = levela_coverage(game)
    dependencies = source_dependencies(game, SCENES)
    contexts, destinations = load_contexts(game)
    all_levela_names = [*SCENES, *(row["scene"] for row in coverage["scenes"] if row["scene"] not in SCENES)]
    _, spawn_sources = load_contexts(game, all_levela_names)
    patches = read_depth_patches((game / "EvilDead.exe").read_bytes())
    audit = activation_audit(game, scenes=LEGACY_SCENES)
    probe = run_probe(game, SCENES)
    initial = base.export_scene(game, output)
    actor = {key: initial[key] for key in ACTOR_KEYS}
    manifests = {}
    for name, context in contexts.items():
        manifest = portal_manifest(context["portals"], destinations)
        manifests[name] = manifest
        if name not in ("ak01", "ak09"):
            spawn, role = arrival_spawn(name, contexts, spawn_sources, dependencies["literal_scene_switches"])
            empty_doors = {"scene_name": name, "doors": [], "unsupported_portals": [],
                           "policy": "Original JPC owns all passage enable state; portals.bin retains source geometry."}
            base.export_location(game, output / name, name, context, actor, output,
                                 empty_doors, depth_patches=patches, spawn=spawn, spawn_role=role)
        (output / name / "portals.bin").write_bytes(pack_portals(manifest))
        (output / name / "portals.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    shutil.copyfile(output / "ak01/portals.bin", output / "portals.bin")
    movies = export_runtime_movies(game, output, identities=[movie["id"] for movie in dependencies["movies"]])
    objects = export_runtime_objects(game, output, scene_names=SCENES)
    export_runtime_vfx(game, output)
    export_runtime_audio(game, output, scene_names=SCENES)
    (output / "scripts/activation-audit.json").write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")
    return publish_metadata(output, coverage, dependencies, manifests, movies, objects, probe)


def publish_metadata(output, coverage, dependencies, manifests, movies, objects, probe, *, scenes=SCENES, initial_scene="ak01", level="A"):
    (output / "scripts/scene-bootstrap-probe.json").write_text(json.dumps(probe, indent=2) + "\n", encoding="utf-8")
    (output / ("levela-coverage.json" if level == "A" else "levelb-coverage.json")).write_text(json.dumps(coverage, indent=2) + "\n", encoding="utf-8")
    (output / "source-dependencies.json").write_text(json.dumps(dependencies, indent=2) + "\n", encoding="utf-8")
    metadata = {"scenes": list(scenes), "level": level, "portal_format": "EDPOR1", "source_script_sha256": coverage["source_script_sha256"],
                "coverage": {"source_factories": len(coverage["scenes"]), "exported": len(scenes),
                             "excluded": [{"scene": row["scene"], "reason": row["reason"], "unsupported": row["unsupported_actor_prototypes"]}
                                          for row in coverage["scenes"] if not row["included"]],
                             "bounded_bootstrap_completed": len(probe["completed"]), "strict_probe_gaps": probe["strict_gaps"]},
                "movie_ids": [hex(movie["id"]) for movie in movies],
                "dynamic_objects": {"meshes": len(objects["models"]), "textures": len(objects["textures"])},
                "portals": {name: {"count": len(item["portals"]),
                                    "available": sum(p["destination_available"] for p in item["portals"]),
                                    "unavailable_destinations": sorted({p["destination_scene"] for p in item["portals"]
                                                                         if not p["destination_available"]})}
                            for name, item in manifests.items()},
                "limits": "Only listed scenes have renderer assets. Other original destinations retain metadata and require an explicit unavailable-scene result. Original JPC decides state; no new interaction gates are inferred."}
    (output / "episode.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    (output / "world.txt").write_text(f"EDWORLD1\n{initial_scene}\n{len(scenes)}\n" + "\n".join(scenes) + "\n", encoding="ascii")
    return metadata


def levelb_coverage(game):
    from export_actor import LEVEL_B_ACTORS
    package = ScriptPackage((game / "data/script/pc/levelB.jpc").read_bytes())
    prototypes = {prototype: definition["native_type"] for definition in LEVEL_B_ACTORS.values() for prototype in definition["prototypes"]}
    records = []
    for row in registry(package, "LevelB"):
        calls = []
        for method in package.methods:
            if method["class_name"] != row["class"]:
                continue
            code = package.disassemble(method["index"])
            for index, item in enumerate(code):
                target = item.get("method", {})
                if (target.get("class_name"), target.get("name")) != ("SpawnCommander", "spawnSetSpawnObjectType"):
                    continue
                args = literal_arguments(code, index, 1)
                if args is None or args[0] not in prototypes:
                    raise ValueError("LevelB scene requests an unverified actor resource prototype")
                calls.append({"method": method["index"], "pc": item["offset"], "prototype": args[0], "native_type": prototypes[args[0]]})
        included = row["scene"] != "bb01"
        records.append({**row, "included": included, "spawn_calls": calls, "unsupported_actor_prototypes": [],
                        "reason": "Original B actor resources decoded; gameplay support is separate." if included else "Menu scene, no playable arrival marker."})
    return {"source_script_sha256": hashlib.sha256(package.data).hexdigest(), "scenes": records,
            "limits": "Resource/format coverage only; this does not enable B native gameplay or invent scene activation policy."}


def cached_levelb_rooms(output, scenes, contexts, script_hash):
    """Retain only complete rooms with matching original package/scene bytes."""
    output = Path(output).resolve()
    manifest = output / "episode.json"
    if not manifest.is_file():
        return set()
    previous = json.loads(manifest.read_text(encoding="utf-8"))
    if not isinstance(previous, dict):
        raise ValueError("Invalid existing LevelB export manifest")
    names = previous.get("scenes", [])
    if (not isinstance(names, list) or any(not isinstance(name, str) for name in names)
            or previous.get("level") != "B" or previous.get("source_script_sha256") != script_hash
            or not names or len(names) != len(set(names)) or not set(names) <= set(scenes)):
        raise ValueError("Existing export does not match the original verified LevelB profile")
    cached = set()
    for name in names:
        directory = output / name
        if not directory.resolve().is_relative_to(output):
            raise ValueError("Cached LevelB room redirects outside generated output")
        original = directory / "scene.native"
        if original.is_file() and original.read_bytes() != contexts[name]["payload"]:
            raise ValueError(f"Cached LevelB room {name} differs from the original scene")
        files = ["scene.txt", "source.json", "scene.native", "collision.bin", "floor.bin", "triggers.bin",
                 "camera-frames.bin", "doors.bin", "ash.bmp", "ash.bin", "idle.bin", "walk.bin", "font.bmp", "font.txt", "door.wav"]
        for track, data in enumerate(contexts[name]["scene"]["tracks"]):
            for frame in range(len(data["frames"])):
                suffix = str(track) + (f"_{frame}" if frame else "")
                files.extend((f"background_{suffix}.bmp", f"background_{suffix}.depth"))
        if not all((directory / filename).is_file() for filename in files):
            continue  # Repair an interrupted or incomplete room export.
        metadata = json.loads((directory / "source.json").read_text(encoding="utf-8"))
        if metadata.get("scene") != name or metadata.get("scene_id") != contexts[name]["id"]:
            raise ValueError("Cached LevelB room identity differs from its original source")
        if (directory / "collision.bin").read_bytes() != base.pack_collision(contexts[name]["collision"])[0]:
            continue  # A derived boundary update must survive the normal --extend build.
        cached.add(name)
    return cached


def export_levelb_episode(game, output, *, extend=False):
    game, output = Path(game).resolve(), Path(output).resolve()
    if output == game or game in output.parents:
        raise ValueError("Generated assets must be outside original game directory")
    coverage = levelb_coverage(game)
    scenes = ("bm01", *(row["scene"] for row in coverage["scenes"] if row["included"] and row["scene"] != "bm01"))
    dependencies = source_dependencies(game, scenes, level="B")
    contexts, destinations = load_contexts(game, scenes)
    cached = cached_levelb_rooms(output, scenes, contexts, coverage["source_script_sha256"]) if extend else set()
    # Real A→B arrivals are authored in AU04; retain that source without adding
    # an A scene to this B world profile or fabricating a destination pose.
    _, sources = load_contexts(game, (*scenes, "bb01", "au04"))
    patches = read_depth_patches((game / "EvilDead.exe").read_bytes())
    actors_ready = (output / "levelb-actors.json").is_file()
    actor, _ = base.export_shared_assets(game, output, level="B", audio_scenes=scenes, include_actors=not actors_ready)
    manifests = {}
    for name, context in contexts.items():
        spawn, role = arrival_spawn(name, contexts, sources, dependencies["literal_scene_switches"])
        empty = {"scene_name": name, "doors": [], "unsupported_portals": [],
                 "policy": "Original B JPC owns passage state. No host interaction gates are inferred."}
        if name not in cached:
            base.export_location(game, output / name, name, context, actor, output, empty,
                                 depth_patches=patches, spawn=spawn, spawn_role=role)
        manifest = portal_manifest(context["portals"], destinations)
        manifests[name] = manifest
        (output / name / "portals.bin").write_bytes(pack_portals(manifest))
        (output / name / "portals.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    for path in (output / "bm01").iterdir():
        if path.is_file():
            shutil.copyfile(path, output / path.name)
    movies = export_runtime_movies(game, output, identities=[movie["id"] for movie in dependencies["movies"]])
    objects = export_runtime_objects(game, output, scene_names=scenes, level="B")
    export_runtime_vfx(game, output)
    probe = {"status": "not_executed", "completed": [], "strict_gaps": [],
             "limits": "B startup/activation runs in the separate actual C++ VM probe. The A fixture is not reused or presented as B execution."}
    result = publish_metadata(output, coverage, dependencies, manifests, movies, objects, probe,
                              scenes=scenes, initial_scene="bm01", level="B")
    return {**result, "incremental_preserved": [name for name in scenes if name in cached],
            "incremental_exported": [name for name in scenes if name not in cached]} if extend else result


def extend_episode(game, output):
    """Add newly verified rooms and refresh shared catalogs, preserving room pixels."""
    game, output = Path(game).resolve(), Path(output).resolve()
    if output == game or game in output.parents:
        raise ValueError("Generated assets must be outside the original game directory")
    previous = json.loads((output / "episode.json").read_text(encoding="utf-8"))
    old_scenes = previous["scenes"]
    if not old_scenes or len(set(old_scenes)) != len(old_scenes) or not set(old_scenes) <= set(SCENES):
        raise ValueError("Existing export is not a subset of the verified LevelA profile")
    added = [name for name in SCENES if name not in old_scenes]
    if not added:
        raise ValueError("No newly verified rooms to add")
    coverage, dependencies = levela_coverage(game), source_dependencies(game, SCENES)
    contexts, destinations = load_contexts(game)
    names = [*SCENES, *(row["scene"] for row in coverage["scenes"] if row["scene"] not in SCENES)]
    _, sources = load_contexts(game, names)
    actor = json.loads((output / "source.json").read_text(encoding="utf-8"))
    actor = {key: actor[key] for key in ACTOR_KEYS}
    patches = read_depth_patches((game / "EvilDead.exe").read_bytes())
    for name in added:
        spawn, role = arrival_spawn(name, contexts, sources, dependencies["literal_scene_switches"])
        empty = {"scene_name": name, "doors": [], "unsupported_portals": [],
                 "policy": "Original JPC owns passage enable state; no host interaction gates."}
        base.export_location(game, output / name, name, contexts[name], actor, output, empty,
                             depth_patches=patches, spawn=spawn, spawn_role=role)
    manifests = {}
    for name, context in contexts.items():
        manifest = portal_manifest(context["portals"], destinations)
        manifests[name] = manifest
        (output / name / "portals.bin").write_bytes(pack_portals(manifest))
        (output / name / "portals.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        path = output / name / "source.json"
        metadata = json.loads(path.read_text(encoding="utf-8"))
        role = metadata.get("spawn_role")
        if isinstance(role, dict) and role.get("source_scene") in SCENES and not role.get("source_scene_available", True):
            role["source_scene_available"] = True
            role["role"] = "original in-episode portal arrival marker"
            path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    shutil.copyfile(output / "ak01/portals.bin", output / "portals.bin")
    # Refresh only actor resources/catalogs; the existing28 room backgrounds are untouched.
    from export_actor import export_actors
    export_actors(game, output)
    movies = export_runtime_movies(game, output, identities=[movie["id"] for movie in dependencies["movies"]])
    objects = export_runtime_objects(game, output, scene_names=SCENES)
    # VFX is executable-global, independent of these new rooms. Preserve the
    # owner's current definition/golden bundle during an incremental extension.
    export_runtime_audio(game, output, scene_names=SCENES)
    result = publish_metadata(output, coverage, dependencies, manifests, movies, objects, run_probe(game, SCENES))
    return {**result, "incremental_added": added}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=base.ROOT / "game")
    parser.add_argument("--profile", choices=("A", "B"), default="A")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--extend", action="store_true", help="Add newly verified rooms without rewriting existing room backgrounds")
    args = parser.parse_args()
    output = args.output or base.ROOT / ("build/episode-fullA/assets" if args.profile == "A" else "build/levelb/assets")
    try:
        if args.profile == "B":
            result = export_levelb_episode(args.game, output, extend=args.extend)
        else:
            result = extend_episode(args.game, output) if args.extend else export_episode(args.game, output, profile=args.profile)
    except (OSError, ValueError, KeyError, IndexError) as error:
        parser.exit(1, f"Error: {error}\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
