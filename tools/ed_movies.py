"""Map original FMV IDs and decode Bink movies for bounded native playback."""

import argparse
from fractions import Fraction
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MOVIES = (0x61303100, 0x61303162, 0x61303600)
MOVIE_HEADER = struct.Struct("<8s10I2Q")


def movie_name(identity):
    identity &= 0xFFFFFFFF
    if identity == 0xBEAD0002:
        return "ed2ab"
    chars = [(identity >> shift) & 255 for shift in (24, 16, 8, 0)]
    if (chars[0] not in (ord("a"), ord("b")) or not all(48 <= c <= 57 for c in chars[1:3])
            or chars[3] and not 97 <= chars[3] <= 122):
        raise ValueError(f"Unsupported original movie ID {identity:08x}")
    return chr(chars[0]) + "x" + chr(chars[1]) + chr(chars[2]) + (chr(chars[3]) if chars[3] else "")


def movie_id(name):
    if name == "ed2ab":
        return 0xBEAD0002
    match = re.fullmatch(r"([ab])x([0-9])([0-9])([a-z]?)", name)
    if not match:
        raise ValueError("Unsupported original movie basename")
    a, b, c, suffix = match.groups()
    return ord(a) << 24 | ord(b) << 16 | ord(c) << 8 | (ord(suffix) if suffix else 0)


def bink_header(data):
    if len(data) < 44:
        raise ValueError("Truncated Bink header")
    tag, size, frames, largest, reserved, width, height, numerator, denominator, flags, tracks = struct.unpack_from("<4s10I", data)
    if (tag != b"BIKi" or size + 8 != len(data) or not 0 < frames <= 100000
            or not 0 < largest <= len(data) or not 0 < width <= 2048 or not 0 < height <= 2048
            or not numerator or not denominator or not Fraction(1, 10) <= Fraction(numerator, denominator) <= 120
            or tracks > 8):
        raise ValueError("Unsupported/inconsistent original Bink header")
    table = 44 + 12 * tracks
    if table + frames * 4 > len(data):
        raise ValueError("Truncated Bink frame index")
    positions = [struct.unpack_from("<I", data, table + i * 4)[0] & ~1 for i in range(frames)] + [len(data)]
    if positions[0] < table + frames * 4 or any(a >= b for a, b in zip(positions, positions[1:])):
        raise ValueError("Invalid original Bink frame ranges")
    audio = []
    for index in range(tracks):
        rate, audio_flags = struct.unpack_from("<HH", data, 44 + tracks * 4 + index * 4)
        if not 8000 <= rate <= 192000:
            raise ValueError("Unsupported original Bink audio rate")
        audio.append({"sample_rate": rate, "channels": 2 if audio_flags & 0x2000 else 1,
                      "codec": "binkaudio_dct" if audio_flags & 0x1000 else "binkaudio_rdft", "flags": audio_flags})
    scaling = flags & 0x70000000
    if scaling not in (0, 0x10000000):
        raise ValueError("Unsupported original Bink presentation scaling")
    return {"width": width, "height": height, "display_width": width,
            "display_height": height * (2 if scaling else 1), "frames": frames, "fps_numerator": numerator,
            "fps_denominator": denominator, "duration": frames * denominator / numerator,
            "audio": audio, "flags": flags, "reserved_frame_word": reserved,
            "source_bytes": len(data), "source_sha256": hashlib.sha256(data).hexdigest()}


def decoder(name):
    path = shutil.which(name)
    if not path:
        raise ValueError(f"Installed {name} is required to decode original Bink movies")
    return str(Path(path).resolve())


def command(arguments, **options):
    completed = subprocess.run(arguments, check=False, stderr=subprocess.PIPE, timeout=240,
                               creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0, **options)
    if completed.returncode:
        raise ValueError("Original movie decoding failed: " + completed.stderr.decode("utf-8", "replace")[-4000:])
    return completed.stdout


def probe_movie(path, ffprobe=None):
    metadata = bink_header(Path(path).read_bytes())
    info = json.loads(command([ffprobe or decoder("ffprobe"), "-v", "error", "-show_streams", "-show_format", "-of", "json", str(path)], stdout=subprocess.PIPE))
    video = [stream for stream in info["streams"] if stream["codec_type"] == "video"]
    audio = [stream for stream in info["streams"] if stream["codec_type"] == "audio"]
    if (len(video) != 1 or video[0]["codec_name"] != "binkvideo" or video[0]["width"] != metadata["width"]
            or video[0]["height"] != metadata["height"] or Fraction(video[0]["avg_frame_rate"]) != Fraction(metadata["fps_numerator"], metadata["fps_denominator"])
            or abs(float(info["format"]["duration"]) - metadata["duration"]) > 0.00001 or len(audio) != len(metadata["audio"])):
        raise ValueError("FFprobe disagrees with original Bink header")
    for stream, source in zip(audio, metadata["audio"]):
        if stream["codec_name"] != source["codec"] or int(stream["sample_rate"]) != source["sample_rate"] or stream["channels"] != source["channels"]:
            raise ValueError("FFprobe disagrees with original Bink audio format")
    return metadata


def sample_movie(path, metadata=None, ffmpeg=None):
    metadata = metadata or bink_header(Path(path).read_bytes())
    indices = sorted({0, metadata["frames"] // 2, metadata["frames"] - 1})
    select = "+".join(f"eq(n\\,{index})" for index in indices)
    raw = command([ffmpeg or decoder("ffmpeg"), "-v", "error", "-xerror", "-nostdin", "-threads", "1", "-i", str(path),
                   "-map", "0:v:0", "-an", "-vf", "select=" + select, "-fps_mode", "passthrough", "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1"], stdout=subprocess.PIPE)
    frame_bytes = metadata["width"] * metadata["height"] * 3
    if len(raw) != frame_bytes * len(indices):
        raise ValueError("Decoder did not return exact first/middle/last source frames")
    return [(index, raw[i * frame_bytes:(i + 1) * frame_bytes]) for i, index in enumerate(indices)]


def export_decoded_movie(source, directory, identity, metadata, converter, ffmpeg):
    """Commit the same bounded native movie format for a checked source codec."""
    if len(metadata["audio"]) != 1:
        raise ValueError("Native movie export requires one original audio track")
    target = directory / f"{identity:08x}.edm"
    if target.resolve().parent != directory:
        raise ValueError("Movie output contains a redirected file")
    audio = metadata["audio"][0]
    video_bytes = metadata["width"] * metadata["height"] * 3 * metadata["frames"]
    if video_bytes > 4 * 1024**3:
        raise ValueError("Decoded movie exceeds native byte limit")
    sidecar = directory / f"{identity:08x}.json"
    cached = None
    if target.is_file() and sidecar.is_file():
        try:
            cached = json.loads(sidecar.read_text("utf-8"))
            if (cached["source_sha256"] != metadata["source_sha256"] or cached["format"] != "EDMOVIE1" or cached["converter"] != converter
                    or cached["decoded_sha256"] != file_hash(target) or cached["decoded_bytes"] != target.stat().st_size):
                cached = None
        except (OSError, ValueError, KeyError):
            cached = None
    if cached is None:
        with tempfile.TemporaryDirectory(prefix=".movie-", dir=directory, ignore_cleanup_errors=True) as scratch_name:
            scratch = Path(scratch_name)
            video, pcm, combined = scratch / "video.rgb", scratch / "audio.pcm", scratch / "movie.edm"
            command([ffmpeg, "-v", "error", "-xerror", "-nostdin", "-threads", "1", "-i", str(source),
                     "-map", "0:v:0", "-an", "-fps_mode", "passthrough", "-pix_fmt", "rgb24", "-f", "rawvideo", str(video)], stdout=subprocess.DEVNULL)
            command([ffmpeg, "-v", "error", "-xerror", "-nostdin", "-i", str(source),
                     "-map", "0:a:0", "-vn", "-c:a", "pcm_s16le", "-f", "s16le", str(pcm)], stdout=subprocess.DEVNULL)
            audio_bytes = pcm.stat().st_size
            if (video.stat().st_size != video_bytes or not 0 < audio_bytes <= 128 * 1024**2
                    or audio_bytes % (2 * audio["channels"]) or abs(audio_bytes / (2 * audio["channels"] * audio["sample_rate"]) - metadata["duration"]) > 0.25):
                raise ValueError("Decoded video/audio byte count or duration disagrees with source")
            samples = sample_movie(source, metadata, ffmpeg)
            frame_bytes = metadata["width"] * metadata["height"] * 3
            with video.open("rb") as frames:
                for index, pixels in samples:
                    frames.seek(index * frame_bytes)
                    if frames.read(frame_bytes) != pixels:
                        raise ValueError("Sampled source frame differs from complete decoded stream")
            header = MOVIE_HEADER.pack(b"EDMOVIE1", metadata["width"], metadata["height"], metadata["frames"],
                metadata["fps_numerator"], metadata["fps_denominator"], audio["sample_rate"], audio["channels"],
                audio_bytes // (2 * audio["channels"]), metadata["display_width"], metadata["display_height"], video_bytes, audio_bytes)
            with combined.open("xb") as stream:
                stream.write(header)
                for part in (video, pcm):
                    with part.open("rb") as data:
                        shutil.copyfileobj(data, stream, 1024 * 1024)
                stream.flush()
                os.fsync(stream.fileno())
            cached = {**metadata, "id": identity, "source_name": source.name, "format": "EDMOVIE1",
                "converter": converter,
                "decoded_bytes": combined.stat().st_size, "decoded_sha256": file_hash(combined),
                "sample_frames": [{"frame": i, "rgb24_sha256": hashlib.sha256(pixels).hexdigest()} for i, pixels in samples],
                "audio_frames": audio_bytes // (2 * audio["channels"]), "audio_bytes": audio_bytes}
            if file_hash(source) != metadata["source_sha256"]:
                raise ValueError("Original movie changed during decoding")
            if sidecar.resolve().parent != directory:
                raise ValueError("Movie sidecar path is redirected")
            # Metadata first: any later failed movie commit leaves the old movie intact and invalidates only its cache.
            atomic_json(sidecar, cached)
            os.replace(combined, target)
    return cached


def export_runtime_movies(game: Path, output: Path, identities=DEFAULT_MOVIES):
    game, output = Path(game).resolve(), Path(output).resolve()
    directory = (output / "movies").resolve()
    if output == game or game in output.parents or output not in directory.parents or game in directory.parents:
        raise ValueError("Movie exports must be outside original game directory")
    ffmpeg, ffprobe = decoder("ffmpeg"), decoder("ffprobe")
    converter = {"ffmpeg_version": command([ffmpeg, "-version"], stdout=subprocess.PIPE).decode("utf-8", "replace").splitlines()[0],
                 "ffmpeg_sha256": file_hash(ffmpeg),
                 "ffprobe_version": command([ffprobe, "-version"], stdout=subprocess.PIPE).decode("utf-8", "replace").splitlines()[0]}
    directory.mkdir(parents=True, exist_ok=True)
    records = []
    for identity in dict.fromkeys(identities):
        identity &= 0xFFFFFFFF
        source = game / "fmv" / (movie_name(identity) + ".bik")
        metadata = probe_movie(source, ffprobe)
        if len(metadata["audio"]) != 1:
            raise ValueError("Native movie export currently requires one original audio track")
        records.append(export_decoded_movie(source, directory, identity, metadata, converter, ffmpeg))
    manifest = directory / "manifest.json"
    if manifest.resolve().parent != directory:
        raise ValueError("Movie manifest path is redirected")
    atomic_json(manifest, {"format": "EDMOVIE1", "movies": records,
        "rights": "Original movie/video/audio content retains its original copyright; decoded locally from the user's copy."})
    return records


def file_hash(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def atomic_json(path, value):
    descriptor, temporary = tempfile.mkstemp(prefix=".movie-meta-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(json.dumps(value, indent=2) + "\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--output", type=Path)
    parser.add_argument("ids", type=lambda value: int(value, 0), nargs="*", default=list(DEFAULT_MOVIES))
    args = parser.parse_args()
    try:
        records = (export_runtime_movies(args.game, args.output, args.ids) if args.output else
            [{"id": movie_id(path.stem), "name": path.name, **bink_header(path.read_bytes())} for path in sorted((args.game / "fmv").glob("*.bik"))])
        print(json.dumps(records, indent=2))
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
