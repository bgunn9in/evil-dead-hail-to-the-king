"""Audit original LevelB actors/dependencies; no runtime/exporter changes."""

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import struct

import capstone
from capstone.x86_const import X86_OP_IMM, X86_OP_MEM, X86_OP_REG

from audit_scene_coverage import audit, literal_arguments
from ed_actions import EXE_SHA256, _va_reader
from ed_assets import parse_archive
from ed_model import decode_animations, decode_mesh, decode_skeleton, decode_texture
from ed_scripts import ScriptPackage, _integer


def script_capacities(package):
    method, = [m for m in package.methods if m["class_name"] == "ActorDB" and m["name"] == "initialize"]
    code, result, base = package.disassemble(method["index"]), {}, 0
    for i, ins in enumerate(code):
        if ins.get("field_index") != 54 or ins["mnemonic"] != "getstatic":
            continue
        tail = code[i + 1:i + 5]
        if (len(tail) != 4 or tail[1]["mnemonic"] != "iload_0" or tail[2]["mnemonic"] != "iastore"
                or tail[3]["mnemonic"] != "iinc" or tail[3]["operands"][0] != 0):
            raise ValueError("Unsupported original ActorDB pool allocation")
        native_type, count = _integer(tail[0]), tail[3]["operands"][1]
        if native_type in result or count <= 0:
            raise ValueError("Invalid original ActorDB pool allocation")
        result[native_type] = {"method": method["index"], "pc": ins["offset"], "base": base, "capacity": count}
        base += count
    return result


def native_actor(read, native_type):
    if not 0 <= native_type < 40:
        raise ValueError("Native actor type is outside descriptor table")
    descriptor = struct.unpack("<6I", read(0x463098 + native_type * 24, 24))
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    branch = struct.unpack("<I", read(0x402BF0 + native_type * 4, 4))[0]
    capacity = None
    for ins in md.disasm(read(branch, 48), branch):
        if ins.mnemonic == "jmp":
            break
        ops = ins.operands
        if (ins.mnemonic == "mov" and len(ops) == 2 and ops[0].type == X86_OP_MEM
                and ops[0].size == 1 and ops[0].mem.disp == 0x910 + native_type):
            capacity = (ops[1].imm if ops[1].type == X86_OP_IMM else
                        {"cl": 1, "bl": 4, "dl": 2}.get(ins.reg_name(ops[1].reg)) if ops[1].type == X86_OP_REG else None)
    if capacity is None:
        raise ValueError("Unsupported native actor pool initializer")
    table, _, _, _, register, _ = descriptor
    registration = list(md.disasm(read(register, 24), register))
    if (len(registration) < 3 or registration[0].mnemonic != "push" or registration[1].mnemonic != "push"
            or registration[2].mnemonic != "call" or registration[0].operands[0].imm != table
            or registration[2].operands[0].imm != 0x418B10):
        raise ValueError("Unsupported original FSM registration")
    count = registration[1].operands[0].imm
    if not 0 < count <= 4096:
        raise ValueError("Invalid original FSM count")
    states, gaps = [], []
    for index in range(count):
        animation, mode, mask, callback, begin, end, size, transitions = struct.unpack("<IIIhBBII", read(table + index * 24, 24))
        if size % 4 or size > 4096:
            raise ValueError("Invalid original FSM transition range")
        rules = [struct.unpack("<BBH", read(transitions + offset, 4)) for offset in range(0, size, 4)]
        if any(flags & ~7 or target >= count and target not in (65534, 65535) for _, flags, target in rules):
            gaps.append({"state": index, "format": "FSM transition flags/target"})
        if mode not in (0, 1) or not 0 <= callback <= 10:
            gaps.append({"state": index, "format": "FSM mode/callback", "mode": mode, "callback": callback})
        states.append({"animation": animation, "callback": callback, "events": [begin, end], "mask": mask})
    return {"descriptor_va": hex(0x463098 + native_type * 24), "descriptor": [hex(v) for v in descriptor],
            "capacity": capacity, "capacity_init_va": hex(branch), "fsm_table": hex(table), "fsm_states": count,
            "fsm_callback_ids": sorted({s["callback"] for s in states}),
            "fsm_event_ids": sorted({v for s in states for v in s["events"] if v}),
            "animation_ids": sorted({s["animation"] for s in states if s["animation"]}), "format_gaps": gaps}


def actor_assets(game, coverage, prototype, scenes, cache, animation_ids):
    ids = prototype["resource_tuple"]
    common = set.intersection(*(set(locations) for locations in prototype["resource_locations"]))
    def copy_size(name):
        return sum(max((r["bytes"] for r in coverage["resources"][f"{identity:08x}"] if r["archive"] == name), default=0)
                   for identity in ids)
    #Shared az00/bz00 entries may be132-byte texture-name stubs. Inspect a
    #complete original copy; do not mistake its location for an autoload rule.
    choices = sorted(common, key=lambda name: (-copy_size(name), name not in scenes, not name.startswith("b"), name))
    if not choices:
        return {"format_gaps": ["No single original archive contains the resource tuple"], "locations": prototype["resource_locations"]}
    name = choices[0]
    if name not in cache:
        cache[name] = (game / f"data/{name}.pc").read_bytes()
    archive = cache[name]
    entries = parse_archive(archive)
    def resource(identity, flag):
        entry, = [e for e in entries if e.resource_id == identity and e.flags == flag]
        return archive[entry.offset:entry.offset + entry.size]
    parts, bones = decode_mesh(resource(ids[0], 0)), decode_skeleton(resource(ids[1], 0))
    texture, texture_name = decode_texture(resource(ids[2], 1))
    covered, packs, events36, active36, events20, gaps = set(), [], Counter(), Counter(), Counter(), []
    for entry in entries:
        if entry.flags != 0 or entry.resource_id >> 24 not in (1, 3, 5, 7):
            continue
        payload = archive[entry.offset:entry.offset + entry.size]
        if len(payload) < 8:
            continue
        count = struct.unpack_from("<I", payload)[0]
        if not 0 < count <= (len(payload) - 4) // 4:
            continue
        offsets = struct.unpack_from(f"<{count}I", payload, 4)
        if any(offset > len(payload) - 40 for offset in offsets):
            continue
        clip_ids = {struct.unpack_from("<I", payload, offset)[0] for offset in offsets}
        if not clip_ids.intersection(animation_ids):
            continue
        clips = decode_animations(payload)
        covered.update(clip["resource_id"] for clip in clips)
        for clip in clips:
            if clip["bone_count"] != len(bones):
                gaps.append({"format": "clip/skeleton bone count mismatch", "clip": hex(clip["resource_id"])})
            events36.update(bytes.fromhex(raw)[2] for raw in clip["events36_raw"])
            active36.update(bytes.fromhex(raw)[2] for raw in clip["events36_raw"] if bytes.fromhex(raw)[3])
            events20.update(bytes.fromhex(raw)[2] for raw in clip["events20_raw"])
        packs.append({"resource": hex(entry.resource_id), "clips": len(clips), "frames": sum(c["frame_count"] for c in clips),
                      "bone_counts": sorted({c["bone_count"] for c in clips}), "sha256": hashlib.sha256(payload).hexdigest()})
    missing = sorted(set(animation_ids) - covered)
    return {"inspection_archive": name + ".pc", "archive_is_a_declaring_scene": name in scenes,
            "archive_sha256": hashlib.sha256(archive).hexdigest(), "parts": len(parts), "bones": len(bones),
            "flat_vertices_all_parts": sum(len(p["triangles"]) * 3 for p in parts), "texture_name": texture_name,
            "texture_size": list(texture.size), "animation_packs": packs, "missing_fsm_clip_ids": [hex(i) for i in missing],
            "raw36_event_kinds": dict(sorted(events36.items())), "raw20_tag_byte2": dict(sorted(events20.items())),
            "enabled_raw36_event_kinds": dict(sorted(active36.items())),
            "format_gaps": gaps + ([{"format": "Missing FSM animation clips", "ids": [hex(i) for i in missing]}] if missing else []),
            "load_limit": "Original byte locations/decoders verified; this does not assert native autoload of another archive."}


def analyze(game, workspace):
    game, workspace = Path(game), Path(workspace)
    coverage = audit(game)
    package = ScriptPackage((game / "data/script/pc/levelB.jpc").read_bytes())
    exe = (game / "EvilDead.exe").read_bytes()
    if hashlib.sha256(exe).hexdigest() != EXE_SHA256:
        raise ValueError("Unverified native executable")
    read, capacities, cache = _va_reader(exe), script_capacities(package), {}
    source = (workspace / "runtime/gameplay_actors.h").read_text(encoding="utf-8")
    match = re.search(r"SupportedTypes\{\{([^}]+)\}\}", source)
    if not match:
        raise ValueError("Runtime supported actor snapshot is not an explicit table")
    supported = {int(v) for v in match[1].split(",")}
    runtime_text = "\n".join(path.read_text(encoding="utf-8") for path in (workspace / "runtime").glob("*.h"))
    runtime_text += "\n" + "\n".join(path.read_text(encoding="utf-8") for pattern in ("*.inl", "*.cpp") for path in (workspace / "runtime").glob(pattern))
    #Bindings may use either bare names or complete "name(descriptor)" keys.
    runtime_literals = set(re.findall(r'"([A-Za-z_][A-Za-z0-9_]*)(?:[^"\\]|\\.)*"', runtime_text))
    scenes = {name: scene for name, scene in coverage["scenes"].items() if scene["level"] == "LevelB"}
    by_type = defaultdict(list)
    for name, scene in scenes.items():
        for call in scene["spawn_type_calls"]:
            if call["actor_type_literal"] is None:
                raise ValueError("Unresolved original LevelB spawn type")
            by_type[call["actor_type_literal"]].append({"scene": name, "method": call["method"], "pc": call["pc"]})
    actors = []
    for actor_type, calls in sorted(by_type.items()):
        prototype = coverage["actor_types"]["LevelB"][actor_type]
        native_type = prototype["native_type"]
        native = native_actor(read, native_type)
        declared = capacities.get(native_type)
        if declared and declared["capacity"] != native["capacity"]:
            raise ValueError("Script/native actor capacities disagree")
        owner = prototype["actor_class"]["class"] if prototype["actor_class"] else None
        own_methods = [m for m in package.methods if m["class_name"] == owner]
        natives, fx = {}, []
        for method in own_methods:
            code = package.disassemble(method["index"])
            for position, ins in enumerate(code):
                target = ins.get("method", {})
                if target.get("class_name") != "ed/base/External":
                    continue
                signature = target["name"] + target["descriptor"]
                natives[signature] = target["name"] in runtime_literals
                if target["name"].startswith("vfx"):
                    fx.append({"method": method["index"], "pc": ins["offset"], "native": signature,
                               "all_literal_args": literal_arguments(code, position, 3 if target["name"] == "vfxSpawnMarker" else 7)})
        assets = actor_assets(game, coverage, prototype, {c["scene"] for c in calls}, cache, native["animation_ids"])
        actors.append({"prototype": actor_type, "native_type": native_type, "class": owner, "callsites": calls,
                       "factory": {key: prototype[key] for key in ("method", "branch_pc", "call_pc")},
                       "resource_tuple": [hex(i) for i in prototype["resource_tuple"]],
                       "resource_locations": prototype["resource_locations"], "script_pool": declared,
                       "native": native, "assets": assets, "runtime_native_type_supported": native_type in supported,
                       "own_class_direct_natives": natives, "own_class_fx_calls": fx})
    scene_reports = {}
    for name, scene in scenes.items():
        archive = (game / f"data/{name}.pc").read_bytes()
        entry, = [e for e in parse_archive(archive) if e.resource_id >> 24 == 15]
        payload = archive[entry.offset:entry.offset + entry.size]
        count, offset = struct.unpack_from("<2I", payload, 8)
        #415F25 categorizes kind5 into scene+64;4164BC..511 emits its scalar+40.
        #Kind7 instead goes to scene+6C and416Dxx unpacks its four code bytes.
        emitters = [{"volume": i, "kind": struct.unpack_from("<I", payload, offset + i * 72 + 64)[0]}
                    for i in range(count) if payload[offset + i * 72] == 5]
        direct = scene["direct_native_calls"]
        callsites = []
        for method in package.methods:
            if method["class_name"] != scene["class"]:
                continue
            for ins in package.disassemble(method["index"]):
                target = ins.get("method", {})
                if target.get("class_name") == "ed/base/External":
                    callsites.append({"method": method["index"], "pc": ins["offset"],
                                      "native": target["name"] + target["descriptor"],
                                      "runtime_literal_found": target["name"] in runtime_literals})
        scene_reports[name] = {"scene_id": scene["scene_id"], "factory_method": scene["factory_method"],
            "spawn_prototypes": sorted({c["actor_type_literal"] for c in scene["spawn_type_calls"]}),
            "camera_count": scene["camera_count"], "parsers": scene["parsers"], "source_emitters_kind5": emitters,
            "direct_native_calls": direct, "native_names_absent_from_runtime_literals": [n for n in direct if n.split("(")[0] not in runtime_literals],
            "direct_native_callsites": callsites,
            "movie_dependencies": [coverage["movies"][f"{i:08x}"] | {"id": hex(i)} for i in scene["direct_movie_literals"]],
            "direct_resource_dependencies": [hex(i) for i in scene["direct_resource_literals"]],
            "direct_sound_dependencies": [hex(i) for i in scene["direct_sound_literals"]],
            "direct_object_calls": scene["literal_object_create_calls"]}
    packages = []
    for native_type in sorted({a["native_type"] for a in actors}):
        variants = [a for a in actors if a["native_type"] == native_type]
        if native_type in supported and len(variants) == 1:
            continue
        packages.append({"native_type": native_type, "class": variants[0]["class"],
                         "prototypes": [a["prototype"] for a in variants],
                         "scenes": sorted({c["scene"] for a in variants for c in a["callsites"]}),
                         "resource_variants": len({tuple(a["resource_tuple"]) for a in variants}),
                         "all_assets_and_FSM_clips_decoded": all(not a["assets"]["format_gaps"] for a in variants),
                         "required_work": ["Original JPC ctor/think/damage proof and native callbacks/actor integration"] +
                             (["Distinct mesh/skeleton/texture/curve assets sharing one native pool; do not alias prototype36/37 to native36/37"] if len(variants) > 1 else [])})
    semantics = {
        "actorEffectReflect(IIIIIII)V": {"native_va": "0x402D50", "end": 0x402D8C,
            "semantics": "Reads actor/skel handle then forwards to40A7A0, which is a singleRET in thisPC build. No reflected visual effect executes."},
        "checkSoftReset()I": {"native_va": "0x4094D0", "end": 0x4094D3,
            "semantics": "XOR EAX,EAX;RET. OriginalPC capability result0, not a gameplay predicate to invent."},
        "reboot()V": {"native_va": "0x418D00", "end": 0x418D3B,
            "semantics": "PrintsReboot, sets pending427F830=1,destination4368CC0=BB01,camera4368CBC=0,marker4368CC4=-1. It is not an emptystub."},
        "sceneSwitch(IIII)V": {"native_va": "0x418CB0", "end": 0x418CF5,
            "semantics": "Converts finalorientation argument to radians, forwards to416540 scene-switch transaction. Must preserve original destination/camera/marker; no fabricated pose."},
        "objFlagsAdd(II)I": {"native_va": "0x40E040", "end": 0x40E05D,
            "registration_va": "0x40DD83..40DD95", "name_va": "0x46A410",
            "semantics": "OR low16mask into objectauxflags word; return signextended resulting16-bitword. Distinct from conditionalXOR objFlagsSub."}}
    gaps = []
    for signature, facts in semantics.items():
        sites = [{"scene": name, **call} for name, scene in scene_reports.items() for call in scene["direct_native_callsites"]
                 if call["native"] == signature and not call["runtime_literal_found"]]
        if sites:
            gaps.append({"signature": signature, **{k: v for k, v in facts.items() if k != "end"}, "callsites": sites,
                         "bounded_code_sha256": hashlib.sha256(read(int(facts["native_va"], 16), facts["end"] - int(facts["native_va"], 16))).hexdigest()})
    vfx_gaps = [{"scene": name, **emitter, "runtime_gap": "No definition for original emitter kind13 in currentSourceVfx"}
                for name, scene in scene_reports.items() for emitter in scene["source_emitters_kind5"] if emitter["kind"] == 13]
    bootstrap = [f for f in coverage["actor_factories"]["LevelB"] if f["caller_class"] == "LevelB"]
    bootstrap_copies = []
    for call in bootstrap:
        for identity in call["resource_tuple"]:
            copies = [r for r in coverage["resources"][f"{identity:08x}"] if r["archive"] in ("az01", "bz01")]
            bootstrap_copies.append({"factory": call["factory"], "resource": hex(identity),
                                     "az01_bz01_byte_identical": len(copies) == 2 and len({r["sha256"] for r in copies}) == 1,
                                     "copies": copies})
    main = (workspace / "runtime/main.cpp").read_text(encoding="utf-8")
    progress = (workspace / "runtime/episode_progress.inl").read_text(encoding="utf-8")
    return {"scope": "Original30LevelB scene factories and conservative literal dependencies; no runtime/export changes or gameplay completion claim.",
        "source": {"levelB_sha256": hashlib.sha256(package.data).hexdigest(), "exe_sha256": EXE_SHA256},
        "summary": {"scenes": len(scenes), "spawn_calls": sum(len(c) for c in by_type.values()),
                    "prototype_ids": sorted(by_type), "native_types": sorted({a["native_type"] for a in actors}),
                    "next_packages": len(packages), "unresolved_mode0_arrivals": sum(not p["arrival_resolved"] for s in scenes.values() for p in s["portals"] if p["mode"] == 0)},
        "runtime_snapshot": {"supported_types": sorted(supported),
                             "gameplay_actors_sha256": hashlib.sha256((workspace / "runtime/gameplay_actors.h").read_bytes()).hexdigest()},
        "bootstrap_actor_calls": bootstrap, "bootstrap_resource_copy_proof": bootstrap_copies,
        "actors": actors, "scenes": scene_reports, "next_bounded_packages": packages,
        "concrete_native_gaps": gaps, "concrete_fx_gaps": vfx_gaps,
        "runtime_level_package_gap": {
            "file": "runtime/main.cpp", "current_constructor": 'game(directory/"scripts/levelA.jpc")',
            "hardcoded_constructor_present": 'game(directory/"scripts/levelA.jpc")' in main,
            "lifecycle_file": "runtime/episode_progress.inl", "current_end_scene_owner": "LevelA",
            "hardcoded_end_scene_owner_present": 'methodIndex("LevelA","endScene"' in progress,
            "required_work": "A verified LevelB package/bootstrap/level boundary. Do not execute LevelB numeric actor IDs against LevelA classes or invent VM-reference migration.",
            "source_example": "ActorDB643 native19: LevelA java/lang/Object; LevelB BadAsh. Native31: LevelA java/lang/Object; LevelB NetherPet."},
        "declared_package_movies": [coverage["movies"][f"{i:08x}"] | {"id": hex(i)}
                                    for i in coverage["packages"]["LevelB"]["declared_movie_literals"]],
        "limits": ["A declared spawn type is a conditional/static requirement, not proof the wave is enabled; actor16 AT01 demonstrated why this distinction matters.",
                   "Runtime literal absence is an explicit source-audit gap; literal presence is not proof of a compatible binding or reachable execution.",
                   "Movie/resource literals include unexecuted branches and omit computed IDs. Movie entries validate original44-byte headers, not full playback.",
                   "Actor raw36/raw20 tags and direct FX calls are retained as dependencies; no unverified visual/contact semantics are invented.",
                   "Native resource locations do not imply autoload; same numeric actor IDs are interpreted in the actual LevelB JPC namespace."]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=Path(__file__).resolve().parents[1] / "game")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "research/levelb-actor-frontier.json")
    args = parser.parse_args()
    game, output = args.game.resolve(), args.output.resolve()
    if output == game or game in output.parents:
        parser.error("Audit output must be outside original game directory")
    result = analyze(game, Path(__file__).resolve().parents[1])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result["summary"], indent=2))
