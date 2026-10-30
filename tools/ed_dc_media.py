"""Decode original Dreamcast STR/SFD into existing native stream/movie formats."""

import argparse
from concurrent.futures import ThreadPoolExecutor
from fractions import Fraction
import json
from pathlib import Path
import struct
import subprocess

from ed_audio import pcm_wave, stream_id
from ed_dc_audio import checked_output
from ed_movies import (atomic_json, command, decoder, export_decoded_movie,
                       file_hash, movie_id)


def str_header(data):
    if len(data) <= 2048 or data[213:224] != b"Sega Stream":
        raise ValueError("Missing original Dreamcast STR header/payload")
    base, rate, codec, alignment, blocks, frames, multiplier, tail = struct.unpack_from("<8I", data)
    if (base, rate, codec, alignment, multiplier) != (1, 44100, 4, 16384, 2):
        raise ValueError("Unverified original Dreamcast STR format")
    payload = len(data) - 2048
    capacity = payload * 2 // (base * multiplier)
    if payload % (alignment * base * multiplier) or not 0 < frames <= capacity:
        raise ValueError("Dreamcast STR block/sample range exceeds source")
    return {"channels": 2, "sample_rate": rate, "bits": 16, "frames": frames,
            "physical_frames": capacity, "padding_frames": capacity - frames,
            "source_header_words": [base, rate, codec, alignment, blocks, frames, multiplier, tail]}


def sfd_metadata(info):
    streams = info.get("streams", [])
    video = [s for s in streams if s.get("codec_type") == "video"]
    audio = [s for s in streams if s.get("codec_type") == "audio"]
    if len(video) != 1 or len(audio) != 1 or len(streams) != 2:
        raise ValueError("Dreamcast SFD must contain one video and one audio stream")
    v, a = video[0], audio[0]
    try:
        fps = Fraction(v["avg_frame_rate"])
        frames = int(v["nb_read_frames"])
        starts = [float(s["start_time"]) for s in streams]
        durations = [float(s["duration"]) for s in streams]
        rate, channels = int(a["sample_rate"]), int(a["channels"])
    except (KeyError, ValueError, ZeroDivisionError) as error:
        raise ValueError("Missing Dreamcast SFD frame/timing metadata") from error
    if (v.get("codec_name") != "mpeg1video" or (v.get("width"), v.get("height")) != (320, 240)
            or fps != 30 or not 0 < frames <= 100000 or a.get("codec_name") != "adpcm_adx"
            or (rate, channels) != (44100, 2) or any(abs(t) > 0.000001 for t in starts)
            or any(abs(t - frames / fps) > 0.25 for t in durations)):
        raise ValueError("Unverified Dreamcast SFD codec, dimensions or timing")
    return {"width": 320, "height": 240, "display_width": 320, "display_height": 240,
            "frames": frames, "fps_numerator": fps.numerator, "fps_denominator": fps.denominator,
            "duration": frames / float(fps), "audio": [{"sample_rate": rate, "channels": channels}],
            "source_streams": streams,
            "presentation": "Native 320x240 canvas; MPEG sample-aspect metadata retained for audit"}


def export_dc_media(source, output, workers=4):
    if type(workers) is not int or not 1 <= workers <= 8:
        raise ValueError("Dreamcast media workers must be 1..8")
    source = Path(source).resolve()
    output = checked_output(source, output)
    if output.exists():
        raise FileExistsError("Dreamcast media output must be a new isolated directory")
    streams, movies = sorted(source.glob("*.STR")), sorted(source.glob("*.SFD"))
    if not streams or not movies:
        raise ValueError("Missing original Dreamcast STR/SFD media")
    # Validate every physical identity before starting any conversion.
    for paths, identity in ((streams, stream_id), (movies, movie_id)):
        ids = [identity(p.stem.lower()) for p in paths]
        if len(ids) != len(set(ids)):
            raise ValueError("Duplicate original Dreamcast media identity")
    ffmpeg, ffprobe = decoder("ffmpeg"), decoder("ffprobe")
    converter = {"ffmpeg_sha256": file_hash(ffmpeg), "ffprobe_sha256": file_hash(ffprobe),
                 "ffmpeg_version": command([ffmpeg, "-version"], stdout=subprocess.PIPE).decode("utf-8").splitlines()[0],
                 "ffprobe_version": command([ffprobe, "-version"], stdout=subprocess.PIPE).decode("utf-8").splitlines()[0]}
    stream_directory, movie_directory = output / "audio/streams", output / "movies"
    stream_directory.mkdir(parents=True)
    movie_directory.mkdir()

    def convert_stream(path):
        metadata = str_header(path.read_bytes())
        before = file_hash(path)
        identity = stream_id(path.stem.lower())
        target = stream_directory / f"{identity:08x}.wav"
        command([ffmpeg, "-v", "error", "-xerror", "-nostdin", "-threads", "1", "-f", "dcstr", "-i", str(path),
                 "-map", "0:a:0", "-map_metadata", "-1", "-af", f"atrim=end_sample={metadata['frames']}",
                 "-fflags", "+bitexact", "-flags:a", "+bitexact", "-c:a", "pcm_s16le", str(target)],
                stdout=subprocess.DEVNULL)
        actual = pcm_wave(target.read_bytes())
        if any(actual[key] != metadata[key] for key in ("channels", "sample_rate", "bits", "frames")):
            raise ValueError("Decoded Dreamcast STR PCM length/format mismatch")
        if file_hash(path) != before:
            raise ValueError("Original Dreamcast STR changed during conversion")
        return {**metadata, "id": identity, "source": path.name, "source_sha256": before,
                "bytes": target.stat().st_size, "sha256": file_hash(target)}

    def convert_movie(path):
        before = file_hash(path)
        info = json.loads(command([ffprobe, "-v", "error", "-count_frames", "-show_streams", "-of", "json", str(path)],
                                  stdout=subprocess.PIPE))
        metadata = sfd_metadata(info)
        metadata.update(source_sha256=before, source_bytes=path.stat().st_size)
        return export_decoded_movie(path, movie_directory, movie_id(path.stem.lower()), metadata, converter, ffmpeg)

    with ThreadPoolExecutor(max_workers=workers) as pool:
        stream_records = list(pool.map(convert_stream, streams))
        movie_records = list(pool.map(convert_movie, movies))
    lines = ["EDSTREAM1", str(len(stream_records))] + [f"{c['id']:08x} {c['bytes']}" for c in stream_records]
    (stream_directory / "manifest.txt").write_text("\n".join(lines) + "\n", encoding="ascii")
    rights = "Original content retains its original copyright; decoded locally from the user's Dreamcast copy."
    atomic_json(stream_directory / "source.json", {"format": "EDSTREAM1", "converter": converter,
                "clips": stream_records, "rights": rights})
    atomic_json(movie_directory / "manifest.json", {"format": "EDMOVIE1", "movies": movie_records, "rights": rights})
    manifest = {"format": "EDDCMEDIA1", "source": str(source), "converter": converter,
                "streams": stream_records, "movies": movie_records, "rights": rights}
    atomic_json(output / "manifest.json", manifest)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--workers", type=int, default=4)
    args = parser.parse_args()
    try:
        result = export_dc_media(args.source, args.output, args.workers)
        print(json.dumps({"streams": len(result["streams"]), "movies": len(result["movies"])}))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
