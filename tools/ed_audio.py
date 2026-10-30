"""Extract original PCM sound bundles, retaining full IDs and package scope."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from ed_assets import parse_archive

ROOT = Path(__file__).resolve().parents[1]
EPISODE = ("ak01", "ak09", "al01", "al03", "ah51", "ah01", "ah02", "ah50")


def pcm_wave(data):
    """Validate a bounded RIFF, including the actual PCM byte count."""
    if len(data) < 44 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("Missing RIFF/WAVE header")
    if struct.unpack_from("<I", data, 4)[0] + 8 != len(data):
        raise ValueError("RIFF length exceeds its source allocation or is inconsistent")
    offset, fmt, samples = 12, None, None
    while offset < len(data):
        if len(data) - offset < 8:
            raise ValueError("Truncated RIFF chunk header")
        kind, size = struct.unpack_from("<4sI", data, offset)
        offset += 8
        if size > len(data) - offset:
            raise ValueError("RIFF chunk exceeds source allocation")
        if kind == b"fmt ":
            if fmt is not None or size < 16:
                raise ValueError("Invalid/duplicate PCM format chunk")
            fmt = struct.unpack_from("<HHIIHH", data, offset)
        elif kind == b"data":
            if samples is not None:
                raise ValueError("Duplicate PCM data chunk")
            samples = size
        offset += size + (size & 1)
    if offset != len(data) or fmt is None or samples is None:
        raise ValueError("Incomplete RIFF format/data chunks")
    encoding, channels, rate, byte_rate, alignment, bits = fmt
    if (encoding != 1 or channels not in (1, 2) or bits not in (8, 16)
            or not 1000 <= rate <= 192000 or alignment != channels * bits // 8
            or byte_rate != rate * alignment or not samples or samples % alignment):
        raise ValueError("Unsupported/inconsistent original PCM format")
    return {"channels": channels, "sample_rate": rate, "bits": bits,
            "frames": samples // alignment, "duration": samples / byte_rate}


def sound_bundle(data):
    """Yield checked table ranges; table sizes are only low16 bits in real assets."""
    if len(data) < 4:
        raise ValueError("Truncated sound bundle")
    count = struct.unpack_from("<I", data)[0]
    if count > 4096 or count > (len(data) - 4) // 12:
        raise ValueError("Sound table exceeds bundle")
    table_end = 4 + count * 12
    rows = [struct.unpack_from("<3I", data, 4 + i * 12) for i in range(count)]
    for index, (identity, offset, size) in enumerate(rows):
        end = rows[index + 1][1] if index + 1 < count else len(data)
        if offset < table_end or offset >= end or end > len(data) or end - offset < 12:
            raise ValueError("Invalid embedded sound range")
        raw = data[offset:end]
        declared = struct.unpack_from("<I", raw, 4)[0] + 8
        # Source allocations may include two-byte alignment, never another sound.
        wave = raw[:declared] if declared <= len(raw) else raw
        record = {"id": identity, "index": index, "offset": offset,
                  "table_size": size, "allocation": end - offset, "wave": wave}
        try:
            if declared <= len(raw) and (len(raw) - declared > 3 or any(raw[declared:])):
                raise ValueError("Unexpected data after embedded RIFF")
            record.update(pcm_wave(wave))
        except ValueError as error:
            record["unsupported"] = str(error)
        yield record


def read_audio_assets(game: Path, scene_names=EPISODE):
    scenes = list(dict.fromkeys(scene_names))
    if any(not re.fullmatch(r"[a-z][a-z][0-9][0-9]", name) or name in ("az01", "bz01") for name in scenes):
        raise ValueError("Invalid scene name")
    sources = [("az01", "global"), ("bz01", "global_b")] + [(name, name) for name in scenes]
    clips, unsupported, source_files = {}, [], {}
    for source, bucket in sources:
        archive = (Path(game) / f"data/{source}.pc").read_bytes()
        source_files[source] = hashlib.sha256(archive).hexdigest()
        for entry in parse_archive(archive):
            # Native41AA30 resolves raw animation-SFX IDs from type0D banks;
            # type0E banks contain the separate JPC sound hashes. Both are the
            # same checked original PCM bundle format, with distinct exact IDs.
            if entry.flags != 1 or entry.resource_id >> 24 not in (13,14):
                continue
            payload = archive[entry.offset:entry.offset + entry.size]
            for clip in sound_bundle(payload):
                clip.update(bucket=bucket, source=source, parent_entry=entry.index,parent_type=entry.resource_id>>24,
                            archive_offset=entry.offset + clip["offset"],
                            sha256=hashlib.sha256(clip["wave"]).hexdigest())
                if "unsupported" in clip:
                    unsupported.append(clip)
                    continue
                key = bucket, clip["id"]
                if key in clips and clips[key]["wave"] != clip["wave"]:
                    raise ValueError(f"Conflicting source sound {bucket}:{clip['id']:08x}")
                clips[key] = clip
    return {"clips": list(clips.values()), "unsupported": unsupported, "source_hashes": source_files}


def stream_name(identity):
    """Native4094f0 ignores low16 bits and prints data/act%c%d.wav."""
    identity &= 0xFFFFFFFF
    letter, number = identity >> 24, identity >> 16 & 255
    if not ord("a") <= letter <= ord("z"):
        raise ValueError("Invalid original stream character")
    return f"act{chr(letter)}{number}"


def stream_id(name):
    match = re.fullmatch(r"act([a-z])(0|[1-9][0-9]{0,2})", name)
    if not match or int(match[2]) > 255:
        raise ValueError("Invalid original standalone stream basename")
    return ord(match[1]) << 24 | int(match[2]) << 16


def read_stream_assets(game):
    clips = []
    for path in sorted((Path(game) / "data").glob("act*.wav")):
        identity = stream_id(path.stem)
        raw = path.read_bytes()
        metadata = pcm_wave(raw)
        # All52 source streams have this exact PCM prefix (some retain LIST/smpl metadata). Do not
        # silently broaden the runtime decoder to unsupported source formats.
        if (raw[12:20] != b"fmt \x10\0\0\0" or raw[36:40] != b"data" or len(raw) < 44 + struct.unpack_from("<I", raw, 40)[0]
                or (metadata["channels"], metadata["sample_rate"], metadata["bits"]) != (2, 44100, 16)):
            raise ValueError(f"Unverified original streamed PCM format: {path.name}")
        clips.append({"id": identity, "source": path.name, "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest(), "wave": raw, **metadata})
    if not clips:
        raise ValueError("No original standalone audio streams")
    return clips


def export_runtime_stream_audio(game, output):
    game, output = Path(game).resolve(), Path(output).resolve()
    if output == game or game in output.parents:
        raise ValueError("Stream exports must be outside original game directory")
    clips = read_stream_assets(game)
    directory = output / "audio/streams"
    if output not in directory.resolve().parents:
        raise ValueError("Stream output escapes generated assets")
    directory.mkdir(parents=True, exist_ok=True)
    lines = ["EDSTREAM1", str(len(clips))]
    for clip in clips:
        path = directory / f"{clip['id']:08x}.wav"
        if output not in path.resolve().parents:
            raise ValueError("Stream output escapes generated assets")
        path.write_bytes(clip["wave"])
        lines.append(f"{clip['id']:08x} {clip['bytes']}")
    metadata = {"format": "EDSTREAM1", "mapping": "data/act%c%d.wav from IDhighByte/nextByte; low16 ignored",
                "clips": [{k: v for k, v in clip.items() if k != "wave"} for clip in clips]}
    for name, data in (("manifest.txt", ("\n".join(lines) + "\n").encode("ascii")),
                       ("source.json", (json.dumps(metadata, indent=2) + "\n").encode("utf-8"))):
        path = directory / name
        if output not in path.resolve().parents:
            raise ValueError("Stream metadata escapes generated assets")
        path.write_bytes(data)
    return {"streams": len(clips), "bytes": sum(c["bytes"] for c in clips)}


def export_runtime_audio(game: Path, output: Path, scene_names=EPISODE):
    game, output = Path(game).resolve(), Path(output).resolve()
    if output == game or game in output.parents:
        raise ValueError("Audio exports must be outside original game directory")
    assets = read_audio_assets(game, scene_names)

    def write(relative, data):
        path = output / relative
        target = path.resolve()
        if output not in target.parents or game == target or game in target.parents:
            raise ValueError("Audio output path escapes generated asset directory")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    lines = ["EDAUDIO1", str(len(assets["clips"]))]
    for clip in assets["clips"]:
        write(f"audio/{clip['bucket']}/{clip['id']:08x}.wav", clip["wave"])
        lines.append(f"{clip['bucket']} {clip['id']:08x} {len(clip['wave'])}")
    for clip in assets["unsupported"]:
        write(f"audio/unsupported/{clip['source']}/{clip['id']:08x}.wav", clip["wave"])
    metadata = {"format": "EDAUDIO1", "source_hashes": assets["source_hashes"],
                "policy": "Scene overrides selected package globals; global=AZ01/levelA, global_b=BZ01/levelB.",
                "clips": [{k: v for k, v in clip.items() if k != "wave"} for clip in assets["clips"]],
                "unsupported": [{k: v for k, v in clip.items() if k != "wave"} for clip in assets["unsupported"]]}
    write("audio/manifest.txt", ("\n".join(lines) + "\n").encode("ascii"))
    write("audio/source.json", (json.dumps(metadata, indent=2) + "\n").encode("utf-8"))
    return {"clips": len(assets["clips"]), "unique_ids": len({c["id"] for c in assets["clips"]}),
            "unsupported": metadata["unsupported"], **export_runtime_stream_audio(game, output)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--output", type=Path, default=ROOT / "build/doors/assets")
    parser.add_argument("scenes", nargs="*", default=list(EPISODE))
    args = parser.parse_args()
    try:
        print(json.dumps(export_runtime_audio(args.game, args.output, args.scenes), indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
