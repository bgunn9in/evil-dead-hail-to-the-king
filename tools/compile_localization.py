"""Compile checked UTF-8 authoring into an optional, declared CP1251 overlay."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from export_localization_catalog import CONTROL, validate_translation
from ed_scripts import ScriptPackage
from ed_vm import Interpreter


def font_charset(path):
    lines = Path(path).read_bytes().splitlines()
    if len(lines) != 4 or lines[0] != b"EDFONT2" or lines[2] != b"CP1251":
        raise ValueError("Localization requires an explicitly declared EDFONT2 CP1251 font")
    dimensions = list(map(int, lines[1].split()))
    if len(dimensions) != 5:
        raise ValueError("Invalid font dimensions")
    width, height, columns, atlas_width, atlas_height = dimensions
    scale = 2 if atlas_width == 512 else 1
    charset = lines[3]
    if (not charset or len(charset) > 256 or not 0 < width <= 256 or not 0 < height <= 256 or
            not columns or atlas_width not in (256, 512) or atlas_height not in (256 * scale, 512 * scale) or
            width * columns > atlas_width // scale or (len(charset) + columns - 1) // columns * height > atlas_height // scale or
            any(code < 32 or code in (127, 152) for code in charset)):
        raise ValueError("Invalid bounded CP1251 font")
    return set(charset)


def translations(catalog, level, charset):
    if catalog.get("format") != "ED-TRANSLATION-AUTHORING1" or catalog.get("runtime_encoding") != "windows-1251":
        raise ValueError("Unsupported translation authoring contract")
    result = {}
    for group in catalog["groups"]:
        english = group["en"]
        if (hashlib.sha256(english.encode("ascii")).hexdigest() != group["key"] or
                CONTROL.findall(english) != group["controls"]):
            raise ValueError("Original English group or controls changed")
        text = group["ru"]
        encoded = validate_translation(group, text)
        if [c for c in text if c in "{}"] != [c for c in english if c in "{}"]:
            raise ValueError("Translation changed source brace markers")
        visible = CONTROL.sub("", text).replace("^", "'").encode("cp1251")
        if any(code != 32 and code not in charset and not (code == 39 and 94 in charset) for code in visible):
            raise ValueError("Translation contains a glyph missing from the declared font")
        for source in group["sources"]:
            if source["level"] != level:
                continue
            key = int(source["scope"], 16), int(source["id"], 16)
            if not 0 <= key[0] <= 0xffffffff or not 0 < key[1] <= 0xffffffff or key in result:
                raise ValueError("Invalid or duplicate scoped translation key")
            result[key] = encoded
    if not result:
        raise ValueError("No translations selected for the package")
    return result


def original_text_keys(data):
    if len(data) < 12 or data[:8] != b"EGTEXT2\0":
        raise ValueError("Expected original EGTEXT2 catalog")
    count, = struct.unpack_from("<I", data, 8)
    if count > 10000:
        raise ValueError("Excessive original text count")
    at, keys = 12, set()
    for _ in range(count):
        if at + 12 > len(data):
            raise ValueError("Truncated original catalog")
        scope, identity, size = struct.unpack_from("<3I", data, at)
        at += 12
        raw = data[at:at + size]
        if (not identity or not 0 < size <= 32767 or len(raw) != size or raw[-1:] != b"\0" or
                any(c > 126 or c < 32 and c not in (9, 10, 13) for c in raw[:-1]) or (scope, identity) in keys):
            raise ValueError("Invalid original catalog record")
        keys.add((scope, identity))
        at += size
    if at != len(data):
        raise ValueError("Trailing original catalog bytes")
    return keys


def pack_text(strings, original):
    if set(strings) != original_text_keys(original):
        raise ValueError("Translation keys differ from the original package catalog")
    result = bytearray(struct.pack("<8s2I", b"EGTEXT3\0", 1251, len(strings)))
    for (scope, identity), text in sorted(strings.items()):
        result += struct.pack("<3I", scope, identity, len(text) + 1) + text + b"\0"
    return bytes(result)


def inventory_text_ids(package, identities):
    vm = Interpreter(package)
    methods = [vm.method_index("ObjDB", name, "(I)I") for name in
               ("getObjectTextInventoryName", "getObjectTextInventoryDescription")]
    return {identity: tuple(vm.invoke(method, args=(identity,)) & 0xffffffff for method in methods)
            for identity in identities}


def inventory_label(text):
    # EDINV labels use the modern plain-text wrapper, not4096C0's GUI formatter.
    text = text.decode("cp1251")
    text = CONTROL.sub(lambda token: "\n" if token[0] in ("@", "\n", "\r") else
                       "        " if token[0] == "\t" else "" if token[0].startswith(("$A", "$C")) else token[0], text)
    return text.replace("^", "'").encode("cp1251")


def pack_inventory(original, strings, resolve):
    if len(original)<16 or original[:8] not in (b"EDINV1\0\0",b"EDINV2\0\0"):
        raise ValueError("Expected declared inventory")
    localized=original[:8]==b"EDINV2\0\0";header=20 if localized else 16
    if len(original)<header or localized and struct.unpack_from('<I',original,8)[0]!=1251:
        raise ValueError("Invalid declared inventory encoding")
    textures, items = struct.unpack_from("<2I", original, header-8)
    if not 0 < textures <= 8 or not 0 < items <= 4096 or header + textures * 12 > len(original):
        raise ValueError("Invalid original inventory counts")
    at, records = header + textures * 12, []
    for _ in range(items):
        if at + 44 > len(original):
            raise ValueError("Truncated original inventory record")
        prefix = original[at:at + 36]
        identity, = struct.unpack_from("<I", prefix)
        name_size, description_size = struct.unpack_from("<2I", original, at + 36)
        at += 44
        if not 0 < name_size <= 256 or not 0 < description_size <= 4096 or at + name_size + description_size > len(original):
            raise ValueError("Invalid original inventory label lengths")
        labels=original[at:at + name_size + description_size]
        if any((c<32 and c!=10 or c in (127,152)) if localized else (c<32 or c>126) for c in labels):
            raise ValueError("Invalid original inventory glyph")
        at += name_size + description_size
        records.append((identity, prefix))
    if at != len(original) or len({i for i, _ in records}) != items:
        raise ValueError("Trailing or duplicate original inventory records")
    ids = resolve([identity for identity, _ in records])
    result = bytearray(struct.pack("<8s3I", b"EDINV2\0\0", 1251, textures, items))
    result += original[header:header + textures * 12]
    for identity, prefix in records:
        name, description = [inventory_label(strings[(0, value)]) for value in ids[identity]]
        if not 0 < len(name) <= 256 or not 0 < len(description) <= 4096 or any(c < 32 and c != 10 for c in name + description):
            raise ValueError("Translated inventory label exceeds its declared bounds")
        result += prefix + struct.pack("<2I", len(name), len(description)) + name + description
    return bytes(result)


def compile_overlay(catalog_path, font_path, assets, package_path, level, output):
    output, assets = Path(output).resolve(), Path(assets).resolve()
    if output.exists() or output == assets or assets in output.parents:
        raise ValueError("Use a new overlay directory outside existing assets")
    package_path = Path(package_path).resolve()
    if tuple(part.lower() for part in package_path.parts[-4:-1]) == ("data", "script", "pc"):
        original_root = package_path.parents[3]
        if output == original_root or original_root in output.parents:
            raise ValueError("Localization output must be outside the original game tree")
    catalog_bytes = Path(catalog_path).read_bytes()
    strings = translations(json.loads(catalog_bytes), level, font_charset(font_path))
    text = pack_text(strings, (assets / "gui/text.bin").read_bytes())
    package = ScriptPackage(package_path.read_bytes())
    inventory = pack_inventory((assets / "inventory.bin").read_bytes(), strings,
                               lambda ids: inventory_text_ids(package, ids))
    output.mkdir(parents=True)
    (output / "gui").mkdir()
    (output / "gui/text-ru.bin").write_bytes(text)
    (output / "inventory.bin").write_bytes(inventory)
    proof = {"format": "ED-LOCALIZATION-OVERLAY1", "level": level, "codepage": 1251,
             "catalog_sha256": hashlib.sha256(catalog_bytes).hexdigest(), "text_keys": len(strings),
             "files": [{"path": name, "sha256": hashlib.sha256(raw).hexdigest(), "bytes": len(raw)}
                       for name, raw in (("gui/text-ru.bin", text), ("inventory.bin", inventory))],
             "originals_unchanged": "Compiler reads original GUI/inventory metadata; only a new overlay directory is written."}
    (output / "localization.json").write_text(json.dumps(proof, indent=2) + "\n", encoding="utf-8")
    return proof


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("catalog", "font", "assets", "package", "output"):
        parser.add_argument("--" + option, type=Path, required=True)
    parser.add_argument("--level", choices=("A", "B"), required=True)
    args = parser.parse_args()
    print(json.dumps(compile_overlay(args.catalog, args.font, args.assets, args.package, args.level, args.output), indent=2))
