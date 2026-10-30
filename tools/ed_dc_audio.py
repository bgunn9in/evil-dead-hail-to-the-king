"""Extract every physical Dreamcast AICA sound row without resolving duplicate IDs.

The temporary STR header is a derived FFmpeg transport, never an original asset.
Runtime assets and codecs are unchanged. Original archives remain read-only.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

from ed_assets import parse_dc_archive
from ed_audio import pcm_wave
from ed_movies import command, decoder, file_hash

ROOT = Path(__file__).resolve().parents[1]
FFMPEG_DCSTR_SOURCE = "https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/dcstr.c"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def dc_sound_bank(data):
    """Read native 12-byte {u32 ID, u32 offset, u16 bytes, u16 rateFlag} rows."""
    if len(data) < 4:
        raise ValueError("Truncated Dreamcast sound bank")
    count = struct.unpack_from("<I", data)[0]
    if count > 4096 or count > (len(data) - 4) // 12:
        raise ValueError("Dreamcast sound table exceeds bank")
    table_end = 4 + count * 12
    clips = []
    for slot in range(count):
        identity, offset, size, flag = struct.unpack_from("<IIHH", data, 4 + slot * 12)
        if not size or offset < table_end or offset > len(data) or size > len(data) - offset:
            raise ValueError(f"Dreamcast sound slot {slot}: declared range exceeds bank")
        raw = data[offset:offset + size]
        clips.append({"slot": slot, "id": identity, "offset": offset,
                      "compressed_bytes": size, "rate_flag": flag,
                      "sample_rate": 11025 if flag else 22050,
                      "frames": size * 2, "sha256": sha256(raw), "adpcm": raw})
    return clips


def inventory_dc_audio(source):
    """Keep all banks, empty tables, physical rows, and archive/entry provenance."""
    source = Path(source).resolve()
    paths = sorted((p for p in source.iterdir() if p.is_file() and p.suffix.upper() == ".DC"),
                   key=lambda p: p.name)
    if not paths:
        raise ValueError("No original Dreamcast .DC archives")
    archives, banks, references = [], [], []
    for path in paths:
        archive = path.read_bytes()
        entries = parse_dc_archive(archive)
        archives.append({"archive": path.name, "bytes": len(archive), "sha256": sha256(archive)})
        for entry in entries:
            if entry.resource_id >> 24 not in (13, 14):
                continue
            bank = {"archive": path.name, "entry": entry.index, "id": entry.resource_id,
                    "flags": entry.flags, "offset": entry.offset, "bytes": entry.size}
            if entry.flags != 1:
                references.append(bank)
                continue
            payload = archive[entry.offset:entry.offset + entry.size]
            clips = dc_sound_bank(payload)
            for clip in clips:
                clip["archive_offset"] = entry.offset + clip["offset"]
            bank.update(sha256=sha256(payload), clips=clips)
            banks.append(bank)
    return {"format": "EDDCAUDIO1", "source": str(source), "archives": archives,
            "banks": banks, "references": references,
            "policy": "Retain all physical rows. Native skips already-loaded IDs; no merge precedence is inferred."}


def inventory_metadata(inventory):
    return {**inventory, "banks": [{**bank, "clips": [{key: value for key, value in clip.items() if key != "adpcm"}
                                                     for clip in bank["clips"]]}
                                   for bank in inventory["banks"]]}


def derived_str(adpcm, sample_rate):
    """Wrap exact source bytes for FFmpeg dcstr's mono ADPCM_AICA decoder."""
    if not 0 < len(adpcm) <= 65535 or sample_rate not in (11025, 22050):
        raise ValueError("Invalid Dreamcast AICA sample bytes/rate")
    header = bytearray(2048)
    struct.pack_into("<7I", header, 0, 1, sample_rate, 4, len(adpcm), 0, len(adpcm) * 2, 1)
    header[213:224] = b"Sega Stream"
    return bytes(header) + adpcm


def decode_aica(adpcm, sample_rate, ffmpeg=None):
    ffmpeg = ffmpeg or decoder("ffmpeg")
    transport = derived_str(adpcm, sample_rate)
    with tempfile.TemporaryDirectory(prefix="evil-dead-dc-audio-") as temporary:
        source, destination = Path(temporary) / "derived.str", Path(temporary) / "decoded.wav"
        source.write_bytes(transport)
        command([str(ffmpeg), "-v", "error", "-xerror", "-nostdin", "-threads", "1",
                 "-f", "dcstr", "-i", str(source), "-map_metadata", "-1", "-fflags", "+bitexact",
                 "-flags:a", "+bitexact", "-c:a", "pcm_s16le", "-f", "wav", str(destination)],
                stdout=subprocess.DEVNULL)
        wave = destination.read_bytes()
    metadata = pcm_wave(wave)
    if (metadata["channels"], metadata["sample_rate"], metadata["bits"], metadata["frames"]) != (
            1, sample_rate, 16, len(adpcm) * 2):
        raise ValueError("Dreamcast decoded PCM channels/rate/frame count mismatch")
    return wave, metadata


def checked_output(source, output):
    source, output = Path(source).resolve(), Path(output).resolve()
    original = (ROOT / "game").resolve()
    if output == source or source in output.parents or output == original or original in output.parents:
        raise ValueError("Dreamcast audio output must be outside original source files")
    return output


def export_dc_audio(source, output, workers=4, inventory_only=False, ffmpeg=None):
    if not isinstance(workers, int) or isinstance(workers, bool) or not 1 <= workers <= 8:
        raise ValueError("Dreamcast conversion workers must be 1..8")
    output = checked_output(source, output)
    if output.exists():
        raise FileExistsError("Dreamcast audio output must be a new isolated directory")
    inventory = inventory_dc_audio(source)
    manifest = inventory_metadata(inventory)
    output.mkdir(parents=True)
    if not inventory_only:
        ffmpeg = ffmpeg or decoder("ffmpeg")
        manifest["converter"] = {
            "ffmpeg_path": str(ffmpeg), "ffmpeg_sha256": file_hash(ffmpeg),
            "ffmpeg_version": command([str(ffmpeg), "-version"], stdout=subprocess.PIPE).decode("utf-8", "replace").splitlines()[0],
            "transport": "Ephemeral derived 2048-byte STR header + exact source AICA ADPCM; no rate/channel resampling",
            "demuxer_source": FFMPEG_DCSTR_SOURCE}
        jobs = []
        for source_bank, bank in zip(inventory["banks"], manifest["banks"]):
            for source_clip, clip in zip(source_bank["clips"], bank["clips"]):
                prefix = Path("clips") / bank["archive"].lower() / f"{bank['entry']:04d}_{bank['id']:08x}" / f"{clip['slot']:04d}_{clip['id']:08x}"
                clip["adpcm_file"] = prefix.with_suffix(".adpcm").as_posix()
                clip["wave_file"] = prefix.with_suffix(".wav").as_posix()
                jobs.append((source_clip["adpcm"], clip))

        def convert(job):
            adpcm, clip = job
            wave, metadata = decode_aica(adpcm, clip["sample_rate"], ffmpeg)
            raw_path, wav_path = output / clip["adpcm_file"], output / clip["wave_file"]
            raw_path.parent.mkdir(parents=True, exist_ok=True)
            with raw_path.open("xb") as stream:
                stream.write(adpcm)
            with wav_path.open("xb") as stream:
                stream.write(wave)
            clip.update(pcm=metadata, wave_bytes=len(wave), wave_sha256=sha256(wave))

        with ThreadPoolExecutor(max_workers=workers) as pool:
            for _ in pool.map(convert, jobs):
                pass
    manifest["counts"] = {"archives": len(manifest["archives"]), "banks": len(manifest["banks"]),
                          "empty_banks": sum(not bank["clips"] for bank in manifest["banks"]),
                          "clips": sum(len(bank["clips"]) for bank in manifest["banks"])}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Extracted original Dreamcast files directory")
    parser.add_argument("output", type=Path, help="New isolated output directory")
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--inventory-only", action="store_true")
    args = parser.parse_args()
    try:
        result = export_dc_audio(args.source, args.output, args.workers, args.inventory_only)
        print(json.dumps({"output": str(args.output), **result["counts"]}))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
