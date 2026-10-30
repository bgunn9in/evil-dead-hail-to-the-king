"""Export exact rigid model/texture pools, retaining original IDs and package scope."""

import argparse
import hashlib
import json
from pathlib import Path
import struct

from ed_assets import parse_dc_archive
from ed_dc_audio import checked_output
from ed_dc_model import decode_mesh, decode_texture
from ed_objects import encode_mesh, encode_texture, transformed


def export_pools(source, output):
    source = Path(source).resolve()
    output = checked_output(source, output)
    if output.exists():
        raise FileExistsError("Dreamcast resource pools require a new isolated output")
    paths = sorted(source.glob("*.DC"))
    if not paths:
        raise ValueError("Missing original Dreamcast archives")
    pools = {level: {} for level in ("A", "B")}
    archives = []
    for path in paths:
        raw = path.read_bytes()
        archive_hash = hashlib.sha256(raw).hexdigest()
        archives.append({"path": path.name, "sha256": archive_hash})
        for entry in parse_dc_archive(raw):
            kind = entry.resource_id >> 24
            payload = raw[entry.offset:entry.offset + entry.size]
            if not entry.flags and kind == 11:
                parts = decode_mesh(payload)
                encoded, vertices = encode_mesh(entry.resource_id, parts)
                category = "models"
                details = {"part_count": len(parts), "vertices": len(vertices),
                           "part_flags": [p["flags_raw"] for p in parts]}
            elif entry.flags == 1 and kind in (8, 11):
                image, details = decode_texture(payload, entry.resource_id)
                encoded = encode_texture(entry.resource_id, image)
                category, vertices = "textures", None
            else:
                continue
            record = {"archive": path.name, "entry": entry.index, "id": entry.resource_id,
                      "flags": entry.flags, "offset": entry.offset, "bytes": entry.size,
                      "sha256": hashlib.sha256(payload).hexdigest()}
            # AZ00 is a resource superset used for exact-ID lookup, not an audio autoload.
            levels = ("A", "B") if path.name == "AZ00.DC" else (path.name[0],)
            for level in levels:
                key = category, entry.resource_id
                previous = pools[level].get(key)
                if previous:
                    if previous["encoded"] != encoded:
                        raise ValueError(f"Conflicting original {level} {category} {entry.resource_id:08x}")
                    previous["sources"].append(record)
                else:
                    pools[level][key] = {"id": entry.resource_id, "encoded": encoded,
                                         "sources": [record], "details": details, "vertices": vertices}
    output.mkdir(parents=True)
    pending = output / "INCOMPLETE"
    pending.write_text("Original resource export is incomplete.\n", encoding="ascii")
    result = {"format": "EDDCOBJECTS1", "archives": archives, "profiles": {}}
    for level, entries in pools.items():
        target = output / level / "objects"
        (target / "models").mkdir(parents=True)
        (target / "textures").mkdir()
        metadata = {"format": "EDDCOBJECTS1", "models": [], "textures": [],
                    "policy": "Exact source IDs; all rigid type0B models and original type08/0B textures. No inferred resource aliases."}
        golden = bytearray(struct.pack("<8sI", b"EDOBPOSE", sum(k[0] == "models" for k in entries) * 3))
        for (category, identity), item in sorted(entries.items()):
            extension = ".bin" if category == "models" else ".rgba"
            (target / category / f"{identity:08x}{extension}").write_bytes(item["encoded"])
            metadata[category].append({k: v for k, v in item.items() if k not in ("encoded", "vertices")} |
                                      {"encoded_sha256": hashlib.sha256(item["encoded"]).hexdigest()})
            if category == "models":
                for degrees in ((0, 0, 0), (0, 90, 0), (30, 60, 90)):
                    position, height, flags = (2., .5, -1.), 1.25, 3
                    vertices = transformed(item["vertices"], position, degrees, height, flags)
                    golden.extend(struct.pack("<I3f3if2I", identity, *position, *degrees, height, flags, len(vertices)))
                    for vertex in vertices:
                        golden.extend(struct.pack("<8fIHH", *vertex))
        (target / "pose-goldens.bin").write_bytes(golden)
        (target / "source.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
        result["profiles"][level] = {"models": len(metadata["models"]), "textures": len(metadata["textures"])}
    (output / "manifest.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    pending.unlink()
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(export_pools(args.source, args.output)["profiles"]))
    except (OSError, ValueError) as error:
        parser.exit(1, f"Error: {error}\n")
