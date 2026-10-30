"""Read original Dreamcast foreground layers without applying PC depth patches.

The original bitmap contains four two-bit coverage texels per byte. Geometry is
in the native 640x480 presentation; each local texture is 256x256 ARGB4444.
This decoder preserves authored overscan and layer distances. It does not
implement PVR compositing or claim a pixel-exact Vulkan rendering.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

from ed_assets import parse_dc_archive


TEXTURE_SIZE = 256
PRESENTATION_SIZE = (640, 480)
ARGB4444_COVERAGE = (0x0FFF, 0x5FFF, 0xAFFF, 0xFFFF)
# Submitted type-5 vertices: +20 BaseA, +24/+28/+2c RGB. Native texture
# environment 1 modulates RGB but replaces BaseA with the texture alpha.
SOURCE_VERTEX_ALPHA = 0.25
SOURCE_RGB_FACTOR = 0.875
DISTANCE_EPSILON = struct.unpack("<f", struct.pack("<I", 0x322BCC77))[0]
_UNPACK = tuple(bytes(((value >> shift) & 3) * 85 for shift in (6, 4, 2, 0))
                for value in range(256))


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def _f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def unpack_coverage(data):
    """Expand MSB pairs to the alpha bytes represented by 0/5/A/F nibbles."""
    return b"".join(_UNPACK[value] for value in data)


def decode_dc_masks(data):
    """Read count + 12-byte {packed rectangle, float distance, bitmap offset}.

    The native writer advances each destination row by 256 texels even when a
    source row is wider. Keep both the complete expanded rows and that bounded
    texture reconstruction. Bytes written beyond the texture are reported,
    rather than reproducing the original out-of-bounds memory write.
    """
    if len(data) < 4:
        raise ValueError("Truncated Dreamcast foreground count")
    count = struct.unpack_from("<I", data)[0]
    if count > 4096 or count > (len(data) - 4) // 12:
        raise ValueError("Dreamcast foreground table exceeds resource")
    table_end = 4 + count * 12
    layers = []
    for index in range(count):
        packed, distance_bits, offset = struct.unpack_from("<III", data, 4 + index * 12)
        x_groups4, y, pitch_byte, height_byte = struct.pack("<I", packed)
        # 8c09574c..5770 substitutes 256 for either zero dimension byte.
        pitch, height = pitch_byte or 256, height_byte or 256
        size = pitch * height
        if offset < table_end or offset > len(data) or size > len(data) - offset:
            raise ValueError(f"Dreamcast foreground layer {index}: bitmap range exceeds resource")
        distance = struct.unpack("<f", struct.pack("<I", distance_bits))[0]
        if not math.isfinite(distance):
            raise ValueError(f"Dreamcast foreground layer {index}: nonfinite distance")
        # 8c0961a0 stores 1.0 when the original float is below this threshold.
        effective_distance = 1.0 if distance < DISTANCE_EPSILON else distance
        inverse_z = _f32(1.0 / effective_distance)
        raw = data[offset:offset + size]
        coverage = unpack_coverage(raw)
        width = pitch * 4
        native_end = (height - 1) * TEXTURE_SIZE + width
        native = bytearray(max(TEXTURE_SIZE * TEXTURE_SIZE, native_end))
        for row in range(height):
            start = row * TEXTURE_SIZE
            native[start:start + width] = coverage[row * width:(row + 1) * width]
        screen_x, screen_y = x_groups4 * 10.0, y * 1.875
        screen_width, screen_height = pitch * 10.0, height * 1.875
        layers.append({
            "index": index, "packed_rect": packed,
            "x_groups4": x_groups4, "y": y,
            "pitch_byte": pitch_byte, "height_byte": height_byte,
            "pitch_bytes": pitch, "width": width, "height": height,
            "distance_bits": distance_bits, "distance": distance,
            "effective_distance": effective_distance, "native_inverse_z": inverse_z,
            "native_inverse_z_bits": struct.unpack("<I", struct.pack("<f", inverse_z))[0],
            "native_vertex_alpha": SOURCE_VERTEX_ALPHA, "native_rgb_factor": SOURCE_RGB_FACTOR,
            "screen_rect_xywh": [screen_x, screen_y, screen_width, screen_height],
            "local_uv_bounds": [0.0, 0.0, pitch / 64.0, height / 256.0],
            "background_uv_bounds": [screen_x / 1024.0, screen_y / 512.0,
                                     (screen_x + screen_width) / 1024.0,
                                     (screen_y + screen_height) / 512.0],
            "offset": offset, "bytes": size, "sha256": _sha256(raw), "raw": raw,
            "coverage": coverage,
            "native_texture_coverage": bytes(native[:TEXTURE_SIZE * TEXTURE_SIZE]),
            "native_write_spill": bytes(native[TEXTURE_SIZE * TEXTURE_SIZE:]),
            "row_overlap_texels": max(0, width - TEXTURE_SIZE),
            "coverage_histogram": {str(alpha): coverage.count(alpha) for alpha in (0, 85, 170, 255)},
        })
    return layers


def load_dc_masks(archive_path, entry_index):
    """Load an absolute, checked physical archive entry; do not merge IDs."""
    archive_path = Path(archive_path)
    archive = archive_path.read_bytes()
    entries = parse_dc_archive(archive)
    if not 0 <= entry_index < len(entries):
        raise ValueError("Dreamcast foreground entry is out of range")
    entry = entries[entry_index]
    if entry.resource_id >> 24 != 8 or entry.flags != 0:
        raise ValueError("Selected entry is not an original Dreamcast foreground resource")
    payload = archive[entry.offset:entry.offset + entry.size]
    return {"format": "EDDCMASK1", "archive": str(archive_path.resolve()),
            "archive_sha256": _sha256(archive), "entry": entry.index,
            "id": entry.resource_id, "offset": entry.offset, "bytes": entry.size,
            "sha256": _sha256(payload), "presentation_size": list(PRESENTATION_SIZE),
            "texture_size": [TEXTURE_SIZE, TEXTURE_SIZE],
            "argb4444_coverage": list(ARGB4444_COVERAGE),
            "layers": decode_dc_masks(payload)}


def metadata(resource):
    """JSON-safe metadata; raw packed and expanded planes are separate outputs."""
    return {**resource, "layers": [
        {key: value for key, value in layer.items() if not isinstance(value, bytes)}
        | {"native_write_spill_bytes": len(layer["native_write_spill"]),
           "native_texture_sha256": _sha256(layer["native_texture_coverage"]),
           "coverage_sha256": _sha256(layer["coverage"])}
        for layer in resource["layers"]]}


def export_dc_masks(archive_path, entry_index, output):
    """Write a new inspection bundle, never replacing original or runtime data."""
    output = Path(output).resolve()
    source = Path(archive_path).resolve()
    if output == source or output in source.parents:
        raise ValueError("Foreground output must be a new directory separate from its source")
    resource = load_dc_masks(source, entry_index)
    output.mkdir(parents=True, exist_ok=False)
    for layer in resource["layers"]:
        stem = f"layer{layer['index']:03d}"
        for suffix, key in (("2bpp", "raw"), ("coverage8", "coverage"),
                            ("native256.coverage8", "native_texture_coverage"),
                            ("native-spill.coverage8", "native_write_spill")):
            (output / f"{stem}.{suffix}").write_bytes(layer[key])
    (output / "source.json").write_text(json.dumps(metadata(resource), indent=2) + "\n", encoding="utf-8")
    return resource


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("entry", type=int, help="Absolute archive directory entry")
    parser.add_argument("output", type=Path, help="New inspection directory")
    args = parser.parse_args(argv)
    try:
        resource = export_dc_masks(args.archive, args.entry, args.output)
        print(f"Decoded {len(resource['layers'])} original foreground layers into {args.output}")
    except (OSError, ValueError) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
