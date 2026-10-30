"""Read the original Dreamcast English text and create a UTF-8 translation catalog."""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import struct

from ed_assets import parse_archive, parse_dc_archive
from ed_scripts import ScriptPackage

CONTROL = re.compile(r"\$A[0-9]{9}|\$[A-Za-z]|[@\n\r\t]|%[0-9]*[dsifu]")
FONT_PREFIX = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789?!.,;:+-="
METHODS = {
    ("Main", "initialize"), ("Main", "createScene"), ("Game", "showTextBox"),
    ("GameTextBox", "initialize"), ("GameTextBox", "setTextAssetID"),
    ("InvBookText", "initialize"), ("InvBookText", "setTextAssetID"),
    ("Gui", "itemAdvanceText"), ("Gui", "itemSetDataID"),
    ("GuiSys", "setDataID"), ("ObjDB", "getObjectTextInventoryName"),
    ("ObjDB", "getObjectTextInventoryDescription"), ("ObjDB", "getObjectTextInGame"),
    ("MenMain", "itemUse"), ("GameOver", "initialize"),
}
SEED = {
    "Yes": "Да", "No": "Нет", "New Game": "Новая игра",
    "Load Game": "Загрузить игру", "Save Game": "Сохранить игру",
    "Options": "Настройки", "Quit": "Выход", "Exit": "Выход",
    "Back": "Назад", "Cancel": "Отмена", "Continue": "Продолжить",
    "Stereo": "Стерео", "Mono": "Моно", "On": "Вкл.", "Off": "Выкл.",
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def text_payload(raw):
    end = raw.find(b"\0")
    if end < 0 or any(raw[end:]):
        raise ValueError("Text must end in NUL followed only by zero padding")
    value = raw[:end]
    if len(value) > 32766 or any(c > 126 or c < 32 and c not in (9, 10, 13) for c in value):
        raise ValueError("Original text is outside the verified ASCII/cursor contract")
    return value.decode("ascii")


def archive_texts(root, dreamcast):
    records = []
    for path in sorted(Path(root).iterdir()):
        suffix = "DC" if dreamcast else "pc"
        if not re.fullmatch(r"[ab][a-z][0-9]{2}\." + suffix, path.name, re.IGNORECASE):
            continue
        raw = path.read_bytes()
        entries = parse_dc_archive(raw) if dreamcast else parse_archive(raw)
        scope = 0 if path.stem.lower() in ("az01", "bz01") else struct.unpack_from("<I", raw)[0]
        for entry in entries:
            if entry.resource_id >> 24 & 31 != 17:
                continue
            if entry.flags:
                raise ValueError("Original text has an unsupported resource flag")
            payload = raw[entry.offset:entry.offset + entry.size]
            text = text_payload(payload)
            records.append({
                "level": path.stem[0].upper(), "scope": f"{scope:08x}",
                "scope_name": "shared" if not scope else path.stem.lower(),
                "id": f"{entry.resource_id:08x}", "archive": path.name,
                "entry": entry.index, "offset": entry.offset, "bytes": entry.size,
                "sha256": digest(payload), "text": text,
            })
    if not records:
        raise ValueError("No original text resources found")
    return records


def grouped_catalog(records, pc_records=()):
    pc = {(r["level"], r["scope"], r["id"]): r["text"] for r in pc_records}
    groups, seen, forms = {}, {}, Counter()
    for record in records:
        text = record["text"]
        key = f'{record["level"]}:{record["scope"]}:{record["id"]}'
        if key in seen and seen[key] != text:
            raise ValueError("Conflicting original text for one scoped resource")
        seen[key] = text
        group = digest(text.encode("ascii"))
        if group not in groups:
            groups[group] = {
                "key": group, "en": text, "ru": SEED.get(text, "" if not text else None),
                "controls": CONTROL.findall(text), "sources": [],
            }
        source = {k: v for k, v in record.items() if k != "text"}
        source["key"] = key
        source["pc_same_text"] = pc.get((record["level"], record["scope"], record["id"])) == text
        groups[group]["sources"].append(source)
        forms.update(CONTROL.findall(text))
    result = list(sorted(groups.values(), key=lambda r: r["key"]))
    return {
        "format": "ED-TRANSLATION-AUTHORING1", "language": "ru-RU",
        "authoring_encoding": "UTF-8", "runtime_encoding": "windows-1251",
        "rules": [
            "Translate ru only; en and all source keys/hashes remain original.",
            "Preserve the ordered controls list, including colors and authored line breaks.",
            "Caret is original apostrophe spelling, not a protected control token.",
            "Do not substitute the Latin alphabet; Russian uses separately mapped glyphs.",
            "Runtime compiler must reject missing translations and uncovered characters.",
        ],
        "summary": {
            "source_records": len(records), "scoped_keys": len(seen),
            "unique_english": len(groups),
            "unique_english_words": sum(len(re.findall(r"[A-Za-z]+(?:\^[A-Za-z]+)?", r["en"])) for r in result),
            "source_record_words": sum(len(re.findall(r"[A-Za-z]+(?:\^[A-Za-z]+)?", r["text"])) for r in records),
            "source_characters": sum(len(r["text"]) for r in records),
            "control_forms": dict(sorted(forms.items())),
            "seeded_groups": sum(r["ru"] is not None for r in result),
        }, "groups": result,
    }


def validate_translation(group, text):
    if not isinstance(text, str) or "\0" in text:
        raise ValueError("Translation must be a NUL-free Unicode string")
    if CONTROL.findall(text) != group["controls"]:
        raise ValueError("Translation changed source formatting controls")
    result = text.encode("cp1251", errors="strict")
    if len(result) > 32766:
        raise ValueError("Translation exceeds native signed16 byte cursor")
    return result


def method_map(dc, pc):
    result = {}
    for level in "AB":
        raw = (Path(dc) / f"LEVEL{level}.JDC").read_bytes()
        old = (Path(pc) / f"data/script/pc/level{level}.jpc").read_bytes()
        packages = [ScriptPackage(raw), ScriptPackage(old)]
        row = {"dreamcast_sha256": digest(raw), "pc_sha256": digest(old), "byte_equal": raw == old,
               "dreamcast_classes": len(packages[0].classes), "pc_classes": len(packages[1].classes),
               "dreamcast_methods": len(packages[0].methods), "pc_methods": len(packages[1].methods), "methods": []}
        keys = set()
        for package in packages:
            keys.update((m["class_name"], m["name"], m["descriptor"]) for m in package.methods
                        if (m["class_name"], m["name"]) in METHODS)
        for owner, name, signature in sorted(keys):
            methods = []
            for package, data in zip(packages, (raw, old)):
                found = [m for m in package.methods if (m["class_name"], m["name"], m["descriptor"]) == (owner, name, signature)]
                methods.append(None if not found else {
                    "index": found[0]["index"],
                    "code_sha256": digest(data[56 + found[0]["code_offset"]:56 + found[0]["code_offset"] + found[0]["code_length"]]),
                })
            row["methods"].append({"owner": owner, "name": name, "signature": signature,
                                   "dreamcast": methods[0], "pc": methods[1]})
        result[level] = row
    return {"format": "ED-TEXT-METHOD-MAP1", "warning": "Symbol map only: do not replace PC JPC with JDC using fixed numeric IDs.", "packages": result}


def generate(dc, pc, output):
    output, dc, pc = Path(output), Path(dc), Path(pc)
    if output.exists():
        raise FileExistsError("Use a new output directory; never overwrite translator work")
    for original in (dc.resolve(), pc.resolve()):
        if output.resolve() == original or original in output.resolve().parents:
            raise ValueError("Localization output must be outside originals")
    records = archive_texts(dc, True)
    catalog = grouped_catalog(records, archive_texts(pc / "data", False))
    executable = (dc / "1ST_READ.BIN").read_bytes()
    start = executable.find(FONT_PREFIX)
    if start < 0 or executable.find(FONT_PREFIX, start + 1) >= 0:
        raise ValueError("Original font character table is ambiguous")
    end = executable.find(b"\0", start, start + 256)
    if end < 0:
        raise ValueError("Unterminated original font table")
    charset = executable[start:end].decode("ascii")
    font = {"format": "ED-ORIGINAL-FONT-MAP1", "native_character_table_offset": start,
            "charset_ascii": charset, "characters": len(charset),
            "font_source": "FONT5.PVR", "font_sha256": digest((dc / "FONT5.PVR").read_bytes()),
            "proposal": "EDFONT2; 18 24 14 256 512; CP1251; raw charset line. Preserve original cells; append Cyrillic/punctuation.",
            "alias": "When apostrophe27 is absent, resolve it to the original caret5e cell; preserve native caret parsing."}
    output.mkdir(parents=True)
    for name, value in (("catalog.ru.json", catalog), ("method-map.json", method_map(dc, pc)), ("font-map.json", font)):
        (output / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return catalog["summary"]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dreamcast", type=Path, required=True)
    parser.add_argument("--pc-game", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(generate(args.dreamcast, args.pc_game, args.output), ensure_ascii=False, indent=2))
