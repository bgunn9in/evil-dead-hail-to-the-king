"""Export original AA01/BB01 as nonwalking menu contexts, without a fake spawn."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct

from audit_movie_flags import movie_calls
from ed_assets import parse_archive
from ed_camera import parse_scene
from ed_collision import parse_collision
from ed_masks import read_depth_patches
from ed_model import decode_mesh, decode_texture
from ed_movies import bink_header, export_runtime_movies, movie_id, movie_name, file_hash
from ed_objects import encode_mesh, encode_texture
from ed_portals import parse_portals
from ed_scripts import ScriptPackage
from ed_triggers import parse_triggers
from ed_gui_assets import export_gui_assets
import export_scene as base


ROOT = Path(__file__).resolve().parents[1]
MENUS = {"A": "aa01", "B": "bb01"}
TEXT_KINDS = frozenset((0x11, 0x31, 0x51, 0x71, 0x91, 0xB1, 0xD1, 0xF1))
MAIN_GEOMETRY, MAIN_TEXTURE = 0x0B142038, 0x0B942034


def digest(data):
    return hashlib.sha256(data).hexdigest()


def resource_pool(game, level):
    """Retain the finite shared Maya/text pool and all scene-local Maya/text.

    Keeping this pool avoids guessing which GUI IDs an original script may
    choose dynamically. It is a resource catalog, not a claim all are drawn.
    Sound banks, scene/camera data and movies have their existing own formats.
    """
    if level not in MENUS:
        raise ValueError("Unknown original menu package")
    result = {}
    for name in (MENUS[level], "az01" if level == "A" else "bz01"):
        blob = (Path(game) / f"data/{name}.pc").read_bytes()
        for entry in parse_archive(blob):
            if entry.resource_id >> 24 not in ({0x0B} | TEXT_KINDS):
                continue
            key = (entry.resource_id, entry.flags)
            payload = blob[entry.offset:entry.offset + entry.size]
            source = {"archive": name + ".pc", "entry": entry.index,
                      "offset": entry.offset, "bytes": entry.size, "sha256": digest(payload)}
            if key in result:
                if payload != result[key]["payload"]:
                    raise ValueError(f"Conflicting menu resource {entry.resource_id:08x}/{entry.flags}")
                result[key]["sources"].append(source)
            else:
                result[key] = {"id": key[0], "flags": key[1], "payload": payload, "sources": [source]}
    #419170's BEAD aliases are original PC strings, not archive resources.
    #Export their exact bytes too so the renderer needs no English substitutes.
    import pefile
    executable = (Path(game) / "EvilDead.exe").read_bytes()
    pe = pefile.PE(data=executable)
    for index, address in enumerate((0x46D81C, 0x46D810, 0x46D804, 0x46D800, 0x46D7FC), 1):
        offset = pe.get_offset_from_rva(address - pe.OPTIONAL_HEADER.ImageBase)
        end = executable.find(b"\0", offset, offset + 32)
        if end < 0:
            raise ValueError("Original native menu text is unterminated")
        payload = executable[offset:end + 1]
        identity = 0xBEAD0000 | index
        result[(identity, 0)] = {"id": identity, "flags": 0, "payload": payload,
                               "sources": [{"archive": "EvilDead.exe", "offset": offset, "bytes": len(payload),
                                            "va": hex(address), "resolver_va": "0x419170", "sha256": digest(payload)}]}
    return [result[key] for key in sorted(result)]


def pack_resources(records):
    """EDMRES1: <8sI>, then count*(<id:u32,flags:u32,size:u32> + rawbytes)."""
    if len(records) > 2000:
        raise ValueError("Menu resource count exceeds source pool bounds")
    data, seen = bytearray(struct.pack("<8sI", b"EDMRES1\0", len(records))), set()
    for record in records:
        key, raw = (record["id"], record["flags"]), record["payload"]
        if key in seen or not 0 < key[0] <= 0xFFFFFFFF or not 0 <= key[1] <= 0xFFFFFFFF or len(raw) > 64 * 1024**2:
            raise ValueError("Invalid/duplicate menu resource")
        seen.add(key)
        data.extend(struct.pack("<3I", *key, len(raw)))
        data.extend(raw)
    if len(data) > 128 * 1024**2:
        raise ValueError("Menu resource bytes exceed bound")
    return bytes(data)


def read_resources(data):
    """Bounded reference reader used by tests and future MenuContext loaders."""
    if len(data) < 12 or data[:8] != b"EDMRES1\0" or len(data) > 128 * 1024**2:
        raise ValueError("Invalid menu resource header")
    count = struct.unpack_from("<I", data, 8)[0]
    if count > 2000:
        raise ValueError("Invalid menu resource count")
    offset, records = 12, {}
    for _ in range(count):
        if len(data) - offset < 12:
            raise ValueError("Truncated menu resource record")
        identity, flags, size = struct.unpack_from("<3I", data, offset)
        offset += 12
        key = (identity, flags)
        if not identity or key in records or size > 64 * 1024**2 or size > len(data) - offset:
            raise ValueError("Invalid/duplicate menu resource range")
        records[key] = data[offset:offset + size]
        offset += size
    if offset != len(data):
        raise ValueError("Trailing menu resource bytes")
    return records


def menu_context(game, level):
    if level not in MENUS:
        raise ValueError("Unknown original menu package")
    name = MENUS[level]
    blob = (Path(game) / f"data/{name}.pc").read_bytes()
    entries = [entry for entry in parse_archive(blob) if entry.resource_id >> 24 == 15]
    if len(entries) != 1:
        raise ValueError("Expected one original menu scene")
    entry = entries[0]
    payload = blob[entry.offset:entry.offset + entry.size]
    identity = struct.unpack_from("<I", blob)[0]
    scene, collision = parse_scene(payload), parse_collision(payload)
    portals = parse_portals(payload, identity)
    triggers = parse_triggers(payload, camera_events=True)
    if (portals["scene_name"] != name or scene["markers"] or scene["navpoints"] or portals["portals"]
            or triggers["zones"] or collision["solids"] or any(len(track["frames"]) != 1 for track in scene["tracks"])):
        raise ValueError("Original nonwalking menu structure changed")
    return {"id": identity, "name": name, "payload": payload, "scene": scene,
            "collision": collision, "triggers": triggers, "portals": portals,
            "entry": entry.index, "source_archive_sha256": digest(blob)}


def menu_script_audit(game, level):
    raw = (Path(game) / f"data/script/pc/level{level}.jpc").read_bytes()
    package = ScriptPackage(raw)
    owners = {"Scene" + MENUS[level].upper(), "MenMain", "MenSaveLoad", "GuiSys", "InterfaceMenu", "Interface", "Pad", "Cart"}
    names = {"activateScene", "processFrame", "initialize", "itemUse", "actionItem", "actionSys",
             "setMode", "loadGameEnd", "loadGame", "loadGameDone", "gameStateChange", "enable"}
    methods = []
    for method in package.methods:
        if method["class_name"] not in owners or method["name"] not in names:
            continue
        code = package.disassemble(method["index"])
        calls = [{"pc": row["offset"], "method": row["method"]["index"],
                  "owner": row["method"]["class_name"], "name": row["method"]["name"],
                  "descriptor": row["method"]["descriptor"]} for row in code if "method" in row]
        source = raw[56 + method["code_offset"]:56 + method["code_offset"] + method["code_length"]] if method["has_code"] else b""
        methods.append({"method": method["index"], "owner": method["class_name"], "name": method["name"],
                        "descriptor": method["descriptor"], "file_offset": 56 + method["code_offset"] if method["has_code"] else None,
                        "bytes": method["code_length"], "sha256": digest(source), "calls": calls})
    movies = [call for call in movie_calls(package) if call["owner"] == "Scene" + MENUS[level].upper()]
    # Discover the Load availability field from the source call site, then
    # prove its default; no numeric field ID is copied between A/B packages.
    item = next(m for m in package.methods if m["class_name"] == "MenMain" and m["name"] == "itemUse")
    code = package.disassemble(item["index"])
    set_mode = next(i for i, row in enumerate(code) if row.get("method", {}).get("class_name") == "MenSaveLoad")
    guard = next(row for row in reversed(code[:set_mode]) if row["mnemonic"] == "getfield")
    field = guard["field_index"]
    initial = next(m for m in package.methods if m["class_name"] == "MenMain" and m["name"] == "initialize")
    init = package.disassemble(initial["index"])
    default_true = any(row.get("field_index") == field and row["mnemonic"] == "putfield" and i and init[i - 1]["mnemonic"] == "iconst_1"
                       for i, row in enumerate(init))
    if not default_true:
        raise ValueError("Menu Load default is no longer proven")
    return {"package": level, "script_sha256": digest(raw), "methods": methods, "movies": movies,
            "load_available_field": field, "load_initially_available": True,
            "pc_input": {"platform": 0, "confirm_item_event": 111, "item_event99_ignored_on_pc": True,
                         "path": "Pad.actionItem -> MenMain.actionItem -> GuiSys.actionItem -> MenMain.itemUse",
                         "ordinals": {"0": "NewGame", "1": "Load/setMode(2)", "2": "Options", "3": "systemAbort"}},
            "pc_load_boundary": "Source-only: MenMain.initialize sets availability true; only platform3 refreshes it from Cart. Original MenSaveLoad drives Cart.loadGame/Done and legacy loadGameEnd requests restored scene/camera, marker-1 and mode0. The separate modern runtime adapter validates EDSAVE1 fullWorld files, queues real I/O outsideVM and commits a fresh matchingWorld; it never replays legacy Main.restore over that VM. Catalog validity and completion follow actual validation/I/O."}


def media_scope(game, output, level, menu_movies):
    """Keep the package's existing authored media when adding its menu."""
    game, output = Path(game), Path(output)
    scenes, identities = [], []
    episode = output / "episode.json"
    if episode.exists():
        value = json.loads(episode.read_text(encoding="utf-8"))
        if value.get("level", "A") != level:
            raise ValueError("Menu and existing episode package/provenance differ")
        expected = digest((game / f"data/script/pc/level{level}.jpc").read_bytes())
        if value.get("source_script_sha256") != expected:
            raise ValueError("Menu and existing episode package/provenance differ")
        scenes = value.get("scenes")
        if not isinstance(scenes, list) or len(scenes) > 64 or any(not isinstance(name, str) for name in scenes) or len(set(scenes)) != len(scenes):
            raise ValueError("Invalid existing episode scene scope")
        for identity in value.get("movie_ids", []):
            identities.append(int(identity, 0) if isinstance(identity, str) else identity)
    source_audio = output / "audio/source.json"
    if source_audio.exists():
        value = json.loads(source_audio.read_text(encoding="utf-8"))
        if value.get("format") != "EDAUDIO1" or not isinstance(value.get("source_hashes"), dict):
            raise ValueError("Invalid existing audio provenance")
        for name, checksum in value["source_hashes"].items():
            if not re.fullmatch(r"[ab][a-z][0-9]{2}", name):
                raise ValueError("Invalid existing audio archive name")
            if digest((game / f"data/{name}.pc").read_bytes()) != checksum:
                raise ValueError("Existing audio belongs to a different original archive")
            if name not in ("az01", "bz01"):
                scenes.append(name)
    for name in scenes:
        if not isinstance(name, str) or not re.fullmatch(level.lower() + r"[a-z][0-9]{2}", name) or not (game / f"data/{name}.pc").is_file():
            raise ValueError("Invalid or cross-package existing scene scope")
    manifest = output / "movies/manifest.json"
    if manifest.exists():
        value = json.loads(manifest.read_text(encoding="utf-8"))
        if value.get("format") != "EDMOVIE1" or not isinstance(value.get("movies"), list):
            raise ValueError("Invalid existing movie manifest")
        existing = [row["id"] for row in value["movies"]]
        if len(set(existing)) != len(existing):
            raise ValueError("Duplicate existing movie IDs")
        identities.extend(existing)
    identities.extend(movie_id(call["movie"]) for call in menu_movies)
    for identity in identities:
        if type(identity) is not int or not 0 < identity <= 0xFFFFFFFF:
            raise ValueError("Invalid existing movie ID")
        if not (game / "fmv" / (movie_name(identity) + ".bik")).is_file():
            raise ValueError("Existing movie scope has no original source")
    return {"audio_scenes": list(dict.fromkeys([*scenes, MENUS[level]])),
            "movie_ids": list(dict.fromkeys(identities))}


def seed_movie_cache(game, output, cache, identities):
    """Copy only verified generated movie files; the converter rechecks them."""
    game, output, cache = Path(game).resolve(), Path(output).resolve(), Path(cache).resolve()
    if cache == game or game in cache.parents:
        raise ValueError("Movie cache must be a generated directory")
    directory = output / "movies"
    directory.mkdir(exist_ok=True)
    copied = []
    for identity in identities:
        target, sidecar = directory / f"{identity:08x}.edm", directory / f"{identity:08x}.json"
        if target.exists() and sidecar.exists():
            continue
        source = cache / "movies" / target.name
        meta = cache / "movies" / sidecar.name
        if not source.is_file() or not meta.is_file():
            continue
        value = json.loads(meta.read_text(encoding="utf-8"))
        if (value.get("format") != "EDMOVIE1" or value.get("id") != identity
                or value.get("source_sha256") != file_hash(game / "fmv" / (movie_name(identity) + ".bik"))
                or value.get("decoded_bytes") != source.stat().st_size
                or value.get("decoded_sha256") != file_hash(source)):
            raise ValueError("Movie cache does not match original/decoded provenance")
        if target.resolve().parent != directory.resolve() or sidecar.resolve().parent != directory.resolve():
            raise ValueError("Movie cache target is redirected")
        if source.resolve() != target.resolve():
            shutil.copy2(source, target)
            shutil.copy2(meta, sidecar)
        copied.append(identity)
    return copied


def export_menu(game, output, level, *, movies=False, audio=False, cache=None):
    game, output = Path(game).resolve(), Path(output).resolve()
    if output == game or game in output.parents:
        raise ValueError("Menu output must be outside original game directory")
    context = menu_context(game, level)
    pool, audit = resource_pool(game, level), menu_script_audit(game, level)
    scope = media_scope(game, output, level, audit["movies"])
    output.mkdir(parents=True, exist_ok=True)
    directory = output / context["name"]
    directory.mkdir(exist_ok=True)
    patches = read_depth_patches((game / "EvilDead.exe").read_bytes())
    cameras = base.export_camera_frames(game, directory, context["name"], context, depth_patches=patches)
    (directory / "scene.native").write_bytes(context["payload"])
    (directory / "floor.bin").write_bytes(base.pack_floor(context["collision"]["floor"]))
    collision_bytes, _ = base.pack_collision(context["collision"])
    (directory / "collision.bin").write_bytes(collision_bytes)
    (directory / "triggers.bin").write_bytes(base.pack_triggers(context["triggers"]))
    (directory / "menu.txt").write_text(f"EDMENU1\n{context['name']}\n{len(cameras)}\n", encoding="ascii")
    (directory / "menu-resources.bin").write_bytes(pack_resources(pool))
    by_key = {(row["id"], row["flags"]): row for row in pool}
    main_geometry = by_key[(MAIN_GEOMETRY, 0)]["payload"]
    main_texture = by_key[(MAIN_TEXTURE, 1)]["payload"]
    mesh, _ = encode_mesh(MAIN_GEOMETRY, decode_mesh(main_geometry))
    (directory / "main-menu.mesh").write_bytes(mesh)
    (directory / "main-menu.texture").write_bytes(encode_texture(MAIN_TEXTURE, decode_texture(main_texture)[0]))
    base.export_font(game, output)
    gui_assets = export_gui_assets(game, output, level)
    dependencies = []
    for call in audit["movies"]:
        path = game / "fmv" / (call["movie"] + ".bik")
        raw = path.read_bytes()
        dependencies.append({**call, "source": "fmv/" + path.name, "sha256": digest(raw), **bink_header(raw)})
    if movies:
        if cache is not None:
            seed_movie_cache(game, output, cache, [movie_id(call["movie"]) for call in audit["movies"]])
        export_runtime_movies(game, output, scope["movie_ids"])
    if audio:
        from ed_audio import export_runtime_audio
        export_runtime_audio(game, output, scene_names=scope["audio_scenes"])
    floor = context["collision"]["floor"]
    metadata = {"format": "ED-MENU-CONTEXT1", "package": level, "scene": context["name"], "scene_id": context["id"],
                "source": {"archive": context["name"] + ".pc", "archive_sha256": context["source_archive_sha256"],
                           "entry": context["entry"], "scene_sha256": digest(context["payload"])},
                "walking": False, "spawn": None, "markers": [], "navpoints": [], "cameras": cameras,
                "authored_floor": {"vertices": len(floor["vertices"]), "triangles": len(floor["triangles"]),
                                   "bounds_min": floor["bounds_min"], "bounds_max": floor["bounds_max"]},
                "resources": [{key: value for key, value in row.items() if key != "payload"} for row in pool],
                "resource_selection": "All source shared0B geometry/textures and text kinds11/31/51/71/91/B1/D1/F1, plus scene-local copies; exactIDs and bytes retained.",
                "main_menu_maya": {"geometry": MAIN_GEOMETRY, "texture": MAIN_TEXTURE},
                "movie_dependencies": dependencies, "movies_exported": movies, "audio_exported": audio,
                "media_scope": scope,
                "gui_assets": gui_assets,
                "source_script": audit,
                "loader_policy": "EDMENU1 is nonwalking: retain native actor positions, do not validate/invent spawn or force source gameState. Run Main/Interface and source mode transitions; gate gameplay simulation by applied mode. Draw-time floor sampling still runs before original visibility.",
                "rights": "Original content and copyrights remain with their owners; generated locally from the user's copy."}
    (directory / "source.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--output", type=Path, default=ROOT / "build/menu/assets")
    parser.add_argument("--profile", choices=("A", "B"), required=True)
    parser.add_argument("--movies", action="store_true", help="Decode the two original menu movies using the existing cache/converter")
    parser.add_argument("--audio", action="store_true", help="Export original menu/shared sounds using the existing PCM exporter")
    parser.add_argument("--cache", type=Path, help="Reuse verified movies from a previously generated asset directory")
    args = parser.parse_args()
    result = export_menu(args.game, args.output, args.profile, movies=args.movies, audio=args.audio, cache=args.cache)
    print({"scene": result["scene"], "cameras": len(result["cameras"]), "resources": len(result["resources"]),
           "spawn": result["spawn"], "movies_exported": result["movies_exported"], "audio_exported": result["audio_exported"]})


if __name__ == "__main__":
    main()
