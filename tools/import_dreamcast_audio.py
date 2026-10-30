"""Import original Dreamcast PCM using the verified native resident-bank order.

8c067fa0 loads AZ01/BZ01 then retains its sounds (8c076c20/8c098c60).
8c074940 clears unretained sounds (8c076ca0/8c098ca0) before Scene.DC,
then SceneV.DC. Load callbacks 8c07c980/8c07c8c0 skip resident IDs.
AZ00 is retained in the original conversion inventory, not the retail bootstrap.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil

from ed_audio import pcm_wave
from ed_dc_audio import ROOT, inventory_dc_audio, inventory_metadata


def digest(data):
    return hashlib.sha256(data).hexdigest()


def archive_rows(manifest, name):
    archives = {a["archive"] for a in manifest["archives"]}
    if name not in archives:
        raise ValueError("Missing native audio archive: " + name)
    for bank in sorted((b for b in manifest["banks"] if b["archive"] == name), key=lambda b: b["entry"]):
        if bank["flags"] != 1 or (bank["id"] >> 23) & 63 not in (27, 29):
            raise ValueError("Unverified native audio callback")
        for slot, clip in enumerate(bank["clips"]):
            if clip["slot"] != slot:
                raise ValueError("Physical sound row order changed")
            yield bank, clip


def select_audio(manifest, scenes, level):
    """Return only newly allocated rows per scope; first loaded ID owns its lifetime."""
    if level not in ("a", "b") or any(not re.fullmatch(level + r"[a-z][0-9]{2}", s) for s in scenes):
        raise ValueError("Audio scenes do not match the selected level")
    buckets, resident, skipped = {}, {}, []
    for bucket, archive in (("global", "AZ01.DC"), ("global_b", "BZ01.DC")):
        loaded = {}
        for bank, clip in archive_rows(manifest, archive):
            if clip["id"] not in loaded:
                loaded[clip["id"]] = (bank, clip)
        buckets[bucket] = list(loaded.values())
        resident[bucket] = loaded
    base = resident["global" if level == "a" else "global_b"]
    for scene in scenes:
        loaded, rows = dict(base), []
        for archive in (scene.upper() + ".DC", scene.upper() + "V.DC"):
            for bank, clip in archive_rows(manifest, archive):
                if clip["id"] in loaded:
                    owner = loaded[clip["id"]]
                    skipped.append({"scene": scene, "archive": archive, "entry": bank["entry"], "slot": clip["slot"],
                                    "id": clip["id"], "resident_archive": owner[0]["archive"],
                                    "resident_entry": owner[0]["entry"], "resident_slot": owner[1]["slot"]})
                else:
                    loaded[clip["id"]] = (bank, clip)
                    rows.append((bank, clip))
        if len(loaded) > 96:
            raise ValueError("Native 96-sample resident limit exceeded in " + scene)
        buckets[scene] = rows
    return buckets, skipped


def relative_clip(root, value):
    name = PurePosixPath(value)
    if name.is_absolute() or not name.parts or any(p in ("..", ".") for p in name.parts) or "\\" in value or ":" in value:
        raise ValueError("Invalid decoded sound path")
    result = (root / Path(*name.parts)).resolve()
    if not result.is_relative_to(root):
        raise ValueError("Decoded sound path escapes its inventory")
    return result


def checked_wave(decoded, clip):
    path = relative_clip(decoded, clip["wave_file"])
    raw = path.read_bytes()
    if len(raw) != clip["wave_bytes"] or digest(raw) != clip["wave_sha256"] or pcm_wave(raw) != clip["pcm"]:
        raise ValueError("Original decoded PCM changed: " + str(path))
    return raw


def import_audio(decoded, assets, output, level):
    decoded, assets, output = Path(decoded).resolve(), Path(assets).resolve(), Path(output).resolve()
    if output == ROOT / "game" or output.is_relative_to(ROOT / "game"):
        raise ValueError("Cannot write into original PC game files")
    if output.exists() or output == decoded or output.is_relative_to(decoded) or output == assets or output.is_relative_to(assets):
        raise ValueError("Audio import needs a fresh isolated output directory")
    raw_manifest = (decoded / "manifest.json").read_bytes()
    manifest = json.loads(raw_manifest)
    if manifest.get("format") != "EDDCAUDIO1" or "converter" not in manifest:
        raise ValueError("Expected the verified original Dreamcast PCM conversion")
    original = Path(manifest["source"]).resolve()
    if output == original or output.is_relative_to(original):
        raise ValueError("Cannot write into original archives")
    # Reparse original archives. Every physical ID/range/selector must match the converter input.
    expected = inventory_metadata(inventory_dc_audio(original))
    if manifest["archives"] != expected["archives"] or len(manifest["banks"]) != len(expected["banks"]):
        raise ValueError("Original audio archive inventory changed")
    for bank, source_bank in zip(manifest["banks"], expected["banks"]):
        for key, value in source_bank.items():
            if key != "clips" and bank.get(key) != value:
                raise ValueError("Original audio bank metadata changed")
        if len(bank["clips"]) != len(source_bank["clips"]):
            raise ValueError("Original audio physical rows changed")
        for clip, source_clip in zip(bank["clips"], source_bank["clips"]):
            if any(clip.get(k) != v for k, v in source_clip.items()):
                raise ValueError("Original audio row identity changed")
    tokens = (assets / "world.txt").read_text().split()
    if len(tokens) < 3 or tokens[0] != "EDWORLD1" or int(tokens[2]) != len(tokens) - 3:
        raise ValueError("Invalid runtime world manifest")
    scenes = tokens[3:]
    title = "aa01" if level == "a" else "bb01"
    if title not in scenes:
        scenes = [title] + scenes
    if len(scenes) != len(set(scenes)):
        raise ValueError("Duplicate runtime audio scene")
    buckets, skipped = select_audio(manifest, scenes, level)
    records, total = [], 0
    for bucket, rows in buckets.items():
        for bank, clip in rows:
            raw = checked_wave(decoded, clip)
            total += len(raw)
            if len(raw) > 16 * 1024 * 1024 or total > 256 * 1024 * 1024:
                raise ValueError("Runtime original PCM size limit exceeded")
            records.append({"bucket": bucket, "id": clip["id"], "archive": bank["archive"], "entry": bank["entry"],
                            "bank_id": bank["id"], "slot": clip["slot"], "wave_file": clip["wave_file"],
                            "wave_bytes": len(raw), "wave_sha256": clip["wave_sha256"]})
    # The compatibility door channel bypasses AudioBank for this one original ID.
    doors = [(b, c) for b in manifest["banks"] for c in b["clips"] if c["id"] == 0x0e8a4aca]
    if not doors or len({c["wave_sha256"] for _, c in doors}) != 1:
        raise ValueError("Pinned original door PCM is missing or ambiguous")
    door_bank, door_clip = doors[0]
    door = checked_wave(decoded, door_clip)
    output.mkdir(parents=True)
    (output / "door.wav").write_bytes(door)
    lines = ["EDAUDIO1", str(len(records))]
    for record in records:
        identity = f"{record['id']:08x}"
        destination = output / "audio" / record["bucket"] / (identity + ".wav")
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(relative_clip(decoded, record["wave_file"]), destination)
        if digest(destination.read_bytes()) != record["wave_sha256"]:
            raise ValueError("Decoded PCM changed during import")
        lines.append(f"{record['bucket']} {identity} {record['wave_bytes']}")
    catalog = ("\n".join(lines) + "\n").encode()
    (output / "audio" / "manifest.txt").write_bytes(catalog)
    proof = {"format": "EDDCAUDIOIMPORT1", "source_manifest_sha256": digest(raw_manifest), "source": str(original),
             "assets": str(assets), "world_sha256": digest((assets / "world.txt").read_bytes()), "level": level,
             "pinned_door": {"id": door_clip["id"], "archive": door_bank["archive"], "entry": door_bank["entry"],
                             "slot": door_clip["slot"], "wave_file": door_clip["wave_file"], "wave_sha256": digest(door)},
             "catalog_path": "audio/manifest.txt", "catalog_sha256": digest(catalog), "clips": len(records), "wave_bytes": total,
             "policy": "Protected AZ01/BZ01 first; then scene DC physical order, then scene VDC; first resident ID wins. AZ00 is not retail bootstrap.",
             "native_functions": ["8c067fa0", "8c0788a0", "8c07c980", "8c07c8c0", "8c076c20", "8c098c60", "8c074940", "8c076ca0", "8c098ca0"],
             "buckets": {k: len(v) for k, v in buckets.items()}, "records": records, "skipped_resident_rows": skipped}
    (output / "dreamcast-audio-provenance.json").write_text(json.dumps(proof, indent=2) + "\n")
    return proof


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--decoded", required=True, type=Path)
    p.add_argument("--assets", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--level", required=True, choices=("a", "b"))
    args = p.parse_args()
    proof = import_audio(args.decoded, args.assets, args.output, args.level)
    print(json.dumps({k: proof[k] for k in ("format", "clips", "wave_bytes", "catalog_sha256", "buckets")}, indent=2))


if __name__ == "__main__":
    main()
