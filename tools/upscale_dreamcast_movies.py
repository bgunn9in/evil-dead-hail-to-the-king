"""Create an isolated 2x AV1/PCM movie overlay from verified Dreamcast EDMs."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import time
from import_dreamcast_runtime import checked_path, output_guard, SOURCE_SHA

HEADER = struct.Struct("<8s10I2Q")
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


def digest(path):
    sha = hashlib.sha256()
    with Path(path).open("rb") as stream:
        while block := stream.read(1024 * 1024):
            sha.update(block)
    return sha.hexdigest()


def inspect_ivf(path, width, height, frames, fps):
    end = path.stat().st_size
    with path.open("rb") as stream:
        raw = stream.read(32)
        if len(raw) != 32:
            raise ValueError("Truncated IVF")
        header = struct.unpack("<4sHH4sHHIIII", raw)
        if header != (b"DKIF", 0, 32, b"AV01", width, height, fps[0], fps[1], frames, 0):
            raise ValueError("AV1 IVF dimensions/timing/count differ")
        sizes = []
        for n in range(frames):
            raw = stream.read(12)
            if len(raw) != 12:
                raise ValueError("Truncated IVF packet")
            size, pts = struct.unpack("<IQ", raw)
            if not 0 < size <= 16 * 1024**2 or pts != n or size > end - stream.tell():
                raise ValueError("Invalid IVF packet length/timestamp")
            sizes.append(size)
            stream.seek(size, 1)
        if stream.tell() != end:
            raise ValueError("Trailing IVF packets/bytes")
    return max(sizes)


def command(args, log):
    result = subprocess.run(args, stdout=subprocess.DEVNULL, stderr=log,
                            creationflags=NO_WINDOW, timeout=3600)
    if result.returncode:
        raise ValueError(f"Movie tool failed ({result.returncode}); see {log.name}")


def decode_hash(args, expected, log):
    process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=log, creationflags=NO_WINDOW)
    sha, count = hashlib.sha256(), 0
    try:
        while block := process.stdout.read(1024 * 1024):
            sha.update(block)
            count += len(block)
            if count > expected:
                raise ValueError("AV1 decoder produced extra frames")
        if process.wait(timeout=30) or count != expected:
            raise ValueError("AV1 decoded frame count/size mismatch")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
    return sha.hexdigest()


def source_inputs(source, output):
    source = checked_path(source)
    profile = checked_path(source.parent / "dreamcast-runtime.json")
    seal = json.loads(profile.read_text("utf8"))
    if (seal.get("format") != "ED-DREAMCAST-RUNTIME1" or seal.get("status") != "verified-import"
            or seal.get("source_manifest_sha256") != SOURCE_SHA or "visual_upscale" in seal):
        raise ValueError("Requires a verified original Dreamcast profile, not an existing upscale")
    output = output_guard(output, [source.parent])
    original = json.loads(checked_path(source / "manifest.json").read_text("utf8"))
    rows = original["movies"]
    if original["format"] != "EDMOVIE1" or len(rows) != 56 or len({r["id"] for r in rows}) != 56:
        raise ValueError("Requires56 original Dreamcast decoded movies")
    for row in rows:
        if type(row["id"]) is not int or not 0 <= row["id"] <= 0xffffffff:
            raise ValueError("Invalid original movie identity")
        checked_path(source / f'{row["id"]:08x}.edm')
    return source, output, rows


def upscale(source, output, workers=2):
    if type(workers) is not int or not 1 <= workers <= 4:
        raise ValueError("Workers must be1..4")
    source, output, rows = source_inputs(source, output)
    ffmpeg = str(Path(shutil.which("ffmpeg") or "").resolve())
    if not Path(ffmpeg).is_file():
        raise ValueError("FFmpeg with libsvtav1 is required")
    output.mkdir(parents=True)
    (output / "INCOMPLETE").write_text("AV1 conversion/verification incomplete\n")
    movies, checks, scratch = output / "movies", output / "validation", output / "work"
    for folder in (movies, checks, scratch):
        folder.mkdir()
    converter = {"ffmpeg": ffmpeg, "ffmpeg_sha256": digest(ffmpeg),
                 "ffmpeg_version": subprocess.check_output([ffmpeg, "-version"], text=True).splitlines()[0],
                 "encoder": "libsvtav1", "crf": 18, "preset": 8, "logical_processors": 4,
                 "scale": "Lanczos2x; no frame interpolation", "color": "8bitI420/BT601 limited matrix; SVT leaves primaries/transfer unspecified"}
    started = time.monotonic()

    def one(row):
        identity = row["id"]
        stem = f"{identity:08x}"
        old = source / (stem + ".edm")
        with old.open("rb") as stream:
            header = HEADER.unpack(stream.read(64))
        magic, w, h, frames, num, den, rate, channels, samples, dw, dh, video_bytes, audio_bytes = header
        if (magic != b"EDMOVIE1" or (w, h, dw, dh, num, den, rate, channels) != (320, 240, 320, 240, 30, 1, 44100, 2)
                or not 0 < frames <= 100000 or video_bytes != w*h*3*frames
                or audio_bytes != samples*channels*2 or old.stat().st_size != 64+video_bytes+audio_bytes):
            raise ValueError("Unexpected original Dreamcast EDM bounds")
        source_sha = digest(old)
        if source_sha != row["decoded_sha256"]:
            raise ValueError("Sealed source movie hash mismatch")
        ivf, log_path = scratch / (stem + ".ivf"), checks / (stem + ".log")
        start = time.monotonic()
        args = [ffmpeg, "-v", "warning", "-nostdin", "-f", "rawvideo", "-pixel_format", "rgb24",
                "-video_size", "320x240", "-framerate", "30", "-skip_initial_bytes", "64", "-i", str(old),
                "-frames:v", str(frames), "-an", "-vf",
                "scale=640:480:flags=lanczos:out_color_matrix=bt601:out_range=tv",
                "-pix_fmt", "yuv420p", "-color_primaries", "smpte170m", "-color_trc", "smpte170m",
                "-colorspace", "smpte170m", "-color_range", "tv", "-c:v", "libsvtav1",
                "-crf", "18", "-b:v", "0", "-g", "60", "-preset", "8", "-svtav1-params",
                "lp=4:film-grain=0", "-f", "ivf", str(ivf)]
        with log_path.open("wb") as log:
            command(args, log)
            maximum = inspect_ivf(ivf, 640, 480, frames, (30, 1))
            yuv_sha = decode_hash([ffmpeg, "-v", "error", "-xerror", "-nostdin", "-threads", "4",
                                   "-i", str(ivf), "-an", "-fps_mode", "passthrough", "-pix_fmt", "yuv420p",
                                   "-f", "rawvideo", "pipe:1"], 640*480*3//2*frames, log)
            indices = sorted({0, frames//2, frames-1})
            select = "+".join(f"eq(n\\,{n})" for n in indices)
            reference = checks / (stem + ".rgba")
            command([ffmpeg, "-v", "error", "-xerror", "-nostdin", "-threads", "4", "-i", str(ivf),
                     "-an", "-vf", "select="+select+",scale=in_color_matrix=bt601:in_range=tv:out_range=pc",
                     "-fps_mode", "passthrough", "-pix_fmt", "rgba", "-f", "rawvideo", str(reference)], log)
            if reference.stat().st_size != len(indices)*640*480*4:
                raise ValueError("RGBA reference sample count differs")
        target, pending = movies / (stem + ".edm"), movies / (stem + ".edm.pending")
        pcm = hashlib.sha256()
        with pending.open("xb") as destination, old.open("rb") as original_stream:
            destination.write(HEADER.pack(b"EDMOVIE2", 640, 480, frames, num, den, rate, channels,
                                          samples, 640, 480, ivf.stat().st_size, audio_bytes))
            with ivf.open("rb") as video:
                shutil.copyfileobj(video, destination, 1024*1024)
            original_stream.seek(64+video_bytes)
            left = audio_bytes
            while left:
                block = original_stream.read(min(left, 1024*1024))
                if not block:
                    raise ValueError("Truncated original PCM")
                pcm.update(block)
                destination.write(block)
                left -= len(block)
            destination.flush()
            os.fsync(destination.fileno())
        new = {**row, "format": "EDMOVIE2", "width": 640, "height": 480, "display_width": 640,
               "display_height": 480, "input_edm_sha256": source_sha, "input_edm_bytes": old.stat().st_size,
               "converter": converter, "ivf_sha256": digest(ivf), "video_bytes": ivf.stat().st_size,
               "audio_bytes": audio_bytes, "audio_pcm_sha256": pcm.hexdigest(),
               "decoded_i420_sha256": yuv_sha, "reference_rgba_indices": indices,
               "reference_rgba_sha256": digest(reference), "maximum_packet_bytes": maximum,
               "decoded_bytes": pending.stat().st_size, "decoded_sha256": digest(pending),
               "seconds": time.monotonic()-start}
        (movies / (stem + ".json")).write_text(json.dumps(new, indent=2)+"\n", encoding="utf8")
        os.replace(pending, target)
        print(json.dumps({"id": stem, "frames": frames, "bytes": new["decoded_bytes"], "seconds": new["seconds"]}), flush=True)
        return new

    records = []
    with ThreadPoolExecutor(max_workers=workers) as pool:
        tasks = [pool.submit(one, row) for row in rows]
        for task in as_completed(tasks):
            records.append(task.result())
    records.sort(key=lambda r: r["id"])
    result = {"format": "EDMOVIE2", "source_manifest_sha256": digest(source/"manifest.json"),
              "movies": records, "converter": converter, "seconds": time.monotonic()-started,
              "rights": "Derived exclusively from user's original Dreamcast movies; original video/audio copyright retained.",
              "scope": "2x Lanczos+AV1 CRF18; all original frame counts/FPS/PCM retained. Lossy video, no newly generated content."}
    (movies / "manifest.json").write_text(json.dumps(result, indent=2)+"\n", encoding="utf8")
    aliases = output / "levelB/movies"
    aliases.mkdir(parents=True)
    for p in movies.iterdir():
        os.link(p, aliases/p.name)
    (output/"INCOMPLETE").unlink()
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=2)
    args = parser.parse_args()
    result = upscale(args.source, args.output, args.workers)
    print(json.dumps({"movies": len(result["movies"]), "seconds": result["seconds"],
                      "bytes": sum(r["decoded_bytes"] for r in result["movies"])}))
