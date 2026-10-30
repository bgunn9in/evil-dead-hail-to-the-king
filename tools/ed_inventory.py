"""Read inventory metadata by executing original ObjDB lookups and GUI resources."""

import argparse
import json
from pathlib import Path
import struct

from ed_assets import parse_archive
from ed_model import decode_mesh, decode_texture
from ed_scripts import ScriptPackage
from ed_vm import Interpreter
ROOT = Path(__file__).resolve().parents[1]


QUERIES = {
    "flags": "getObjectFlags", "name_resource_id": "getObjectTextInventoryName",
    "description_resource_id": "getObjectTextInventoryDescription",
    "geometry_resource_id": "getObjectGuiGeomID", "shape_index": "getObjectGuiShapeID",
    "texture_resource_id": "getObjectGuiTextureID",
}


def read_inventory_assets(game: Path, item_ids=None) -> dict:
    package = ScriptPackage((game / "data/script/pc/levelA.jpc").read_bytes())
    vm = Interpreter(package)
    methods = {key: vm.method_index("ObjDB", name, "(I)I") for key, name in QUERIES.items()}
    archives = []
    for name in ("az01.pc", "bz01.pc"):
        path = game / "data" / name
        if path.exists():
            blob = path.read_bytes()
            archives.append((name, blob, parse_archive(blob)))
    textures, geometries, items, resource_sources = {}, {}, [], {}

    def resource(identity):
        for name, blob, entries in archives:
            matches = [entry for entry in entries if entry.resource_id == identity]
            if matches:
                if len(matches) != 1:
                    raise ValueError(f"Ambiguous inventory resource {identity:08X}")
                entry = matches[0]
                resource_sources[identity] = {"archive": name, "entry": entry.index}
                return blob[entry.offset:entry.offset + entry.size], entry.index
        raise ValueError(f"Missing inventory resource {identity:08X}")

    def text(identity):
        data, index = resource(identity)
        if not data or not data.endswith(b"\0") or b"\0" in data[:-1]:
            raise ValueError("Inventory text is not a single terminated source string")
        return data[:-1].decode("ascii"), index

    unavailable = []
    if item_ids is None:
        available = {entry.resource_id for _, _, entries in archives for entry in entries}
        item_ids = []
        candidates = sorted({key for instruction in package.disassemble(methods["flags"])
                             for key, _ in instruction.get("cases", [])})
        for identity in candidates:
            metadata = {key: vm.invoke(method, args=(identity,)) & 0xFFFFFFFF for key, method in methods.items()}
            if not all(metadata[key] for key in ("flags", "name_resource_id", "description_resource_id")):
                continue
            if any(metadata[key] not in available for key in ("name_resource_id", "description_resource_id",
                                                            "geometry_resource_id", "texture_resource_id")):
                unavailable.append(identity)
            else:
                item_ids.append(identity)
    seen = set()
    for identity in item_ids:
        if not isinstance(identity, int) or not 0 <= identity <= 0xFFFF:
            raise ValueError("Invalid inventory item identity")
        if identity in seen:
            continue
        seen.add(identity)
        item = {key: vm.invoke(method, args=(identity,)) & 0xFFFFFFFF for key, method in methods.items()}
        if not item["flags"] or not item["name_resource_id"] or not item["description_resource_id"]:
            raise ValueError(f"Item {identity} has no supported original inventory metadata")
        item["item_id"] = identity
        item["name_raw"], item["name_entry"] = text(item["name_resource_id"])
        item["description_raw"], item["description_entry"] = text(item["description_resource_id"])
        geometry_id, texture_id = item["geometry_resource_id"], item["texture_resource_id"]
        if geometry_id not in geometries:
            data, _ = resource(geometry_id)
            geometries[geometry_id] = decode_mesh(data)
        if texture_id not in textures:
            data, _ = resource(texture_id)
            textures[texture_id] = decode_texture(data)[0]
        parts = geometries[geometry_id]
        if item["shape_index"] >= len(parts):
            raise ValueError("Inventory icon shape is missing")
        part = parts[item["shape_index"]]
        if any(bone != 0xFFFF for bone in part["bone_indices"]):
            raise ValueError("Inventory GUI geometry unexpectedly requires a skeleton")
        uv = [min(p[0] for p in part["uv"]), min(p[1] for p in part["uv"]),
              max(p[0] for p in part["uv"]), max(p[1] for p in part["uv"])]
        if not (0 <= uv[0] < uv[2] <= 1 and 0 <= uv[1] < uv[3] <= 1):
            raise ValueError("Inventory icon uses unsupported wrapping UVs")
        image = textures[texture_id]
        item.update(geometry=part, uv_rect=uv,
                    pixel_rect=[round(uv[0] * image.width), round(uv[1] * image.height),
                                round(uv[2] * image.width), round(uv[3] * image.height)])
        items.append(item)
    return {"items": items, "textures": textures, "lookup_methods": methods, "unavailable_item_ids": unavailable,
            "resource_sources": resource_sources,
            "text_encoding": "Original ASCII glyph codes; localized atlas supplies the displayed alphabet."}


def export_runtime_inventory(game: Path, output: Path, item_ids=None):
    """Write checked metadata and original RGBA pixels for the native renderer."""
    game, output = game.resolve(), output.resolve()
    if output == game or game in output.parents:
        raise ValueError("Inventory exports must be outside original game directory")
    data = read_inventory_assets(game, item_ids)
    if not data["items"]:
        raise ValueError("No original inventory assets selected")
    texture_ids = list(data["textures"])
    binary = bytearray(struct.pack("<8s2I", b"EDINV1\0\0", len(texture_ids), len(data["items"])))
    output.mkdir(parents=True, exist_ok=True)
    for identity in texture_ids:
        image = data["textures"][identity].convert("RGBA")
        if not 0 < image.width <= 2048 or not 0 < image.height <= 2048:
            raise ValueError("Inventory atlas exceeds renderer dimensions")
        binary.extend(struct.pack("<3I", identity, image.width, image.height))
        (output / f"inventory_{identity:08x}.rgba").write_bytes(image.tobytes())
    for item in data["items"]:
        name, description = item["name_raw"].encode("ascii"), item["description_raw"].encode("ascii")
        points = item["geometry"]["positions"]
        width = max(p[0] for p in points) - min(p[0] for p in points)
        height = max(p[1] for p in points) - min(p[1] for p in points)
        if height<=0 or width<=0 or not 0 < width / height < 20 or not 0 < len(name) <= 256 or not 0 < len(description) <= 4096:
            raise ValueError("Invalid inventory label or source icon extent")
        binary.extend(struct.pack("<4I5f2I", item["item_id"], item["flags"],
            texture_ids.index(item["texture_resource_id"]), item["shape_index"],
            *item["uv_rect"], width / height, len(name), len(description)))
        binary.extend(name + description)
    (output / "inventory.bin").write_bytes(binary)
    return {"items": len(data["items"]), "atlases": len(texture_ids),
            "item_ids": [item["item_id"] for item in data["items"]]}


def export_inventory_assets(game, output, item_ids):
    game, output = game.resolve(), output.resolve()
    if output == game or game in output.parents:
        raise ValueError("Inventory exports must be outside original game directory")
    data = read_inventory_assets(game, item_ids)
    output.mkdir(parents=True, exist_ok=True)
    for identity, image in data["textures"].items():
        image.save(output / f"inventory_atlas_{identity:08x}.png")
    for item in data["items"]:
        image = data["textures"][item["texture_resource_id"]]
        image.crop(item["pixel_rect"]).save(output / f"inventory_item_{item['item_id']}.png")
    metadata = {key: value for key, value in data.items() if key != "textures"}
    (output / "inventory-assets.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("item_ids", type=int, nargs="+", help="Original item IDs, such as119 or29")
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        result = (export_inventory_assets(args.game, args.output, args.item_ids) if args.output else
                  {key: value for key, value in read_inventory_assets(args.game, args.item_ids).items() if key != "textures"})
        print(json.dumps(result, indent=2))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
