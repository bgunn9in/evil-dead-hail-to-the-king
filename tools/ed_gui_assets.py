"""Export bounded original GUI text, Maya bounds and GameOver sprite assets."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from ed_assets import parse_archive
from ed_model import decode_mesh, decode_texture
from ed_objects import encode_mesh, encode_texture

MODELS = (0x0B142088, 0x0B14208C, 0x0B142188)
TEXTURE = 0x0B942034


def read_assets(game, level):
    if level not in ("A", "B"):
        raise ValueError("Unknown GUI package")
    game = Path(game)
    texts, source_hashes, models, texture = {}, {}, {}, None
    shared = "az01" if level == "A" else "bz01"
    for path in sorted((game / "data").glob(level.lower() + "[a-z][0-9][0-9].pc")):
        raw = path.read_bytes()
        source_hashes[path.name] = hashlib.sha256(raw).hexdigest()
        for entry in parse_archive(raw):
            identity = entry.resource_id
            payload = raw[entry.offset:entry.offset + entry.size]
            if (identity >> 24) & 31 == 17:
                if entry.flags or not payload or payload[-1] or len(payload) > 32767 or b"\0" in payload[:-1]:
                    raise ValueError("Unsupported original GUI text range")
                if any(c > 126 or c < 32 and c not in (9, 10, 13) for c in payload[:-1]):
                    raise ValueError("Unsupported original GUI glyph code")
                scope = 0 if path.stem == shared else struct.unpack_from("<I", raw)[0]
                key = (scope, identity)
                if key in texts and texts[key] != payload:
                    raise ValueError("Conflicting package text ID")
                texts[key] = payload
            if path.stem == shared:
                if identity in MODELS and entry.flags == 0:
                    models[identity] = decode_mesh(payload)
                if identity == TEXTURE and entry.flags == 1:
                    texture = decode_texture(payload)[0]
    if set(models) != set(MODELS) or texture is None:
        raise ValueError("Original GUI Maya/texture resources are missing")
    return {"texts": texts, "models": models, "texture": texture, "source_hashes": source_hashes}


def pack_texts(texts):
    result = bytearray(struct.pack("<8sI", b"EGTEXT2\0", len(texts)))
    for (scope, identity), raw in sorted(texts.items()):
        if not identity or not raw or len(raw) > 32767 or raw[-1]:
            raise ValueError("Invalid GUI text")
        result.extend(struct.pack("<III", scope, identity, len(raw)))
        result.extend(raw)
    return bytes(result)


def pack_layout(models):
    result = bytearray(struct.pack("<8sI", b"EGLAY1\0\0", len(models)))
    for identity, parts in sorted(models.items()):
        result.extend(struct.pack("<II", identity, len(parts)))
        for part in parts:
            x, y, _ = zip(*part["positions"])
            result.extend(struct.pack("<4f", min(x), min(y), max(x), max(y)))
    return bytes(result)


def export_gui_assets(game, output, level="A"):
    game, output = Path(game), Path(output)
    if output.resolve() == game.resolve() or game.resolve() in output.resolve().parents:
        raise ValueError("GUI output must be outside originals")
    data = read_assets(game, level)
    target = output / "gui"
    (target / "models").mkdir(parents=True, exist_ok=True)
    (target / "textures").mkdir(exist_ok=True)
    (target / "text.bin").write_bytes(pack_texts(data["texts"]))
    (target / "layout.bin").write_bytes(pack_layout(data["models"]))
    mesh, _ = encode_mesh(0x0B142088, data["models"][0x0B142088])
    (target / "models/game-over.mesh").write_bytes(mesh)
    (target / "textures/0b942034.rgba").write_bytes(encode_texture(TEXTURE, data["texture"]))
    metadata = {"format": "EDGUI-ASSETS1", "level": level, "text_count": len(data["texts"]),
                "source_hashes": data["source_hashes"], "models": list(MODELS), "game_over_texture": TEXTURE,
                "game_over_part": 0, "text_source": "Original ASCII glyph codes; original font atlas supplies localization.",
                "scope": "Scene-local text overrides sharedAZ01/BZ01. Preserve distinctAC01/AK01 payloads with the same91820800 ID."}
    (target / "source.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return metadata


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=Path(__file__).resolve().parents[1] / "game")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--level", choices=("A", "B"), default="A")
    args = parser.parse_args()
    print(export_gui_assets(args.game, args.output, args.level)["text_count"])
