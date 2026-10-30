"""Decode original foreground bit masks and verified EXE depth corrections."""

import argparse
from array import array
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

from ed_assets import decode_background, parse_archive


AK01 = 0x616B0100
AK09 = 0x616B0900
DEPTH_EXPONENT = 0.8799999952316284
AK01_DEPTH_OVERRIDES = {
    (15, 1): 0.9887149930000305,
    (76, 1): 0.9889780282974243,
    (76, 3): 0.9895139932632446,
    (171, 0): 0.9884359836578369,
}
AK09_DEPTH_OVERRIDES = {
    (19, 0): 0.9981769919395447,
    (33, 2): 0.9965059757232666,
    (47, 0): 0.99617600440979,
    (62, 0): 0.9960770010948181,
    (76, 0): 0.9957090020179749,
    (76, 1): 0.9962069988250732,
    (76, 2): 0.9963070154190063,
}
SCENE_DEPTH_OVERRIDES = {AK01: AK01_DEPTH_OVERRIDES, AK09: AK09_DEPTH_OVERRIDES}
PATCH_START, PATCH_END = 0x42BC66, 0x42CD33
PATCH_SHA256 = "590008a766654106def184f07357b8f9113a8b64c0c8a1c5abb0327c9bd66dc7"


class DepthPatches:
    """Evaluate only the verified original scene/background/layer correction tree."""
    def __init__(self, code, read_float, start=PATCH_START):
        self.start, self.end = start, start + len(code)
        self.instructions = {}
        position = 0
        while position < len(code):
            pc, head = start + position, code[position:position + 2]
            size, record = 0, None
            if head == b"\x81\xf9":
                size, record = 6, ("cmp", "ecx", struct.unpack_from("<I", code, position + 2)[0])
            elif code[position] == 0x3D:
                size, record = 5, ("cmp", "eax", struct.unpack_from("<I", code, position + 1)[0])
            elif head in (b"\x83\xf8", b"\x83\xfe"):
                size, record = 3, ("cmp", "eax" if head[1] == 0xF8 else "esi", struct.unpack_from("<b", code, position + 2)[0])
            elif head == b"\x3b\xf0":
                size, record = 2, ("cmp", "esi", "eax")
            elif head in (b"\x85\xc0", b"\x85\xf6"):
                register = "eax" if head[1] == 0xC0 else "esi"
                size, record = 2, ("test", register, register)
            elif head in (b"\x0f\x84", b"\x0f\x85", b"\x0f\x87"):
                size = 6
                record = ({0x84: "je", 0x85: "jne", 0x87: "ja"}[head[1]],
                          pc + size + struct.unpack_from("<i", code, position + 2)[0])
            elif code[position] in (0x74, 0x75, 0xEB):
                size = 2
                record = ({0x74: "je", 0x75: "jne", 0xEB: "jmp"}[code[position]],
                          pc + size + struct.unpack_from("<b", code, position + 1)[0])
            elif code[position] == 0xE9:
                size, record = 5, ("jmp", pc + 5 + struct.unpack_from("<i", code, position + 1)[0])
            elif head == b"\xdd\xd8":
                size, record = 2, ("discard",)
            elif head == b"\xd9\x05":
                address = struct.unpack_from("<I", code, position + 2)[0]
                depth = read_float(address)
                if not math.isfinite(depth) or not 0 <= depth <= 1:
                    raise ValueError("Invalid original depth correction constant")
                size, record = 6, ("depth", depth, address)
            else:
                raise ValueError(f"Unsupported mask correction instruction at 0x{pc:x}")
            if position + size > len(code):
                raise ValueError("Truncated mask correction instruction")
            self.instructions[pc] = (pc + size, record)
            position += size
        boundaries = {*self.instructions, self.end}
        for _, record in self.instructions.values():
            if record[0] in ("je", "jne", "ja", "jmp") and record[1] not in boundaries:
                raise ValueError("Mask correction branch escapes its verified instruction tree")

    def lookup(self, scene_id, background_index, layer_index):
        values = (scene_id, background_index, layer_index)
        if any(not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFF for value in values):
            raise ValueError("Invalid scene/background/layer identity")
        registers = dict(zip(("ecx", "eax", "esi"), values))
        pc, flags, correction = self.start, None, None
        for _ in range(len(self.instructions) + 1):
            if pc == self.end:
                return correction
            following, instruction = self.instructions[pc]
            kind = instruction[0]
            if kind in ("cmp", "test"):
                a = registers[instruction[1]]
                b = registers[instruction[2]] if isinstance(instruction[2], str) else instruction[2] & 0xFFFFFFFF
                flags = (a == b, a > b) if kind == "cmp" else (not a & b, bool(a & b))
            elif kind in ("je", "jne", "ja", "jmp"):
                if kind != "jmp" and flags is None:
                    raise ValueError("Mask correction branches before comparing")
                if kind == "jmp" or {"je": flags[0], "jne": not flags[0], "ja": flags[1]}[kind]:
                    following = instruction[1]
            elif kind == "depth":
                correction = {"clip_depth": instruction[1], "constant_va": instruction[2], "load_va": pc}
            elif kind == "discard":
                correction = None
            pc = following
        raise ValueError("Mask correction instruction budget exceeded")


def read_depth_patches(executable: bytes) -> DepthPatches:
    """Read PE32 sections and the complete known original depth-correction tree.

    No original code is executed. Its1108 cmp/test/branch/float-load instructions
    are decoded as data, with a verified code hash and checked branch bounds.
    """
    try:
        pe = struct.unpack_from("<I", executable, 0x3C)[0]
        machine, count, _, _, _, optional_size, _ = struct.unpack_from("<HHIIIHH", executable, pe + 4)
        optional, sections = pe + 24, pe + 24 + optional_size
        if (executable[:2] != b"MZ" or executable[pe:pe + 4] != b"PE\0\0" or machine != 0x14C
                or struct.unpack_from("<H", executable, optional)[0] != 0x10B
                or struct.unpack_from("<I", executable, optional + 28)[0] != 0x400000 or count > 96):
            raise ValueError("Unsupported original PE32 executable")
        ranges = []
        for index in range(count):
            virtual_size, rva, raw_size, raw = struct.unpack_from("<4I", executable, sections + index * 40 + 8)
            if raw > len(executable) or raw_size > len(executable) - raw:
                raise ValueError("PE section exceeds executable bounds")
            ranges.append((0x400000 + rva, min(virtual_size, raw_size), raw))

        def read(address, size):
            for start, length, raw in ranges:
                if start <= address and address - start + size <= length:
                    return executable[raw + address - start:raw + address - start + size]
            raise ValueError("Depth correction reference exceeds a PE section")

        code = read(PATCH_START, PATCH_END - PATCH_START)
        if hashlib.sha256(code).hexdigest() != PATCH_SHA256:
            raise ValueError("Original mask correction routine does not match the verified PC version")
        return DepthPatches(code, lambda address: struct.unpack("<f", read(address, 4))[0])
    except (struct.error, IndexError) as error:
        raise ValueError("Truncated original executable depth corrections") from error


def _override(scene_id, background_index, layer_index, depth_patches):
    if depth_patches is not None:
        value = depth_patches.lookup(scene_id, background_index, layer_index)
        return None if value is None else value["clip_depth"]
    if scene_id not in SCENE_DEPTH_OVERRIDES:
        raise ValueError("Pass verified EXE depth patches for scenes beyond ak01/ak09")
    return SCENE_DEPTH_OVERRIDES[scene_id].get((background_index, layer_index))


def mask_clip_depth(distance, *, scene_id, background_index, layer_index,
                    near=0.05000000074505806, far=625.0, depth_patches=None):
    """Original depth, including corrected raw0 or uncorrected positiveInfinity."""
    if (not all(math.isfinite(v) for v in (distance, near, far))
            or not 0 < near < far):
        raise ValueError("Invalid mask distance or clipping planes")
    override = _override(scene_id, background_index, layer_index, depth_patches)
    if override is not None:
        return override
    # 42BC2A..52 divides by0 then original CRT456301..33D returns+Infinity
    # for pow(-Infinity,positive noninteger.88). Keep this nonfinite result;
    # it is not an authored finite occlusion depth.
    if distance == 0:
        return math.inf
    # Source corrections overwrite the computed depth, including AL02's raw0
    # distance. Validate the raw range only when the original keeps its result.
    if not near <= distance <= far:
        raise ValueError("Invalid mask distance or clipping planes")
    return ((1 - near / distance) * far / (far - near)) ** DEPTH_EXPONENT


def decode_masks(data: bytes, tail_offset: int, *, scene_id: int,
                 background_index: int, depth_patches=None) -> list[dict]:
    """Return L640x384 masks, raw camera distances, and original clip depths.

    Each layer is float distance + 15 records of six u32 fields. Record +0x10
    holds an absolute payload offset to a 128x128 LSB-first bit mask, or -1.
    A set bit means the original background pixel occludes geometry behind it.
    """
    from PIL import Image

    if tail_offset < 0 or tail_offset + 4 > len(data):
        raise ValueError("Missing foreground layer count")
    count = struct.unpack_from("<I", data, tail_offset)[0]
    table_offset = tail_offset + 4
    if count > (len(data) - table_offset) // 364:
        raise ValueError("Truncated foreground layer table")
    table_end = table_offset + count * 364
    unpack_bits = [bytes(255 * ((value >> bit) & 1) for bit in range(8))
                   for value in range(256)]
    layers = []
    for layer_index in range(count):
        offset = table_offset + layer_index * 364
        distance = struct.unpack_from("<f", data, offset)[0]
        depth = mask_clip_depth(distance, scene_id=scene_id,
                                background_index=background_index, layer_index=layer_index, depth_patches=depth_patches)
        mask = Image.new("L", (640, 384))
        tile_offsets = []
        for tile_index in range(15):
            bit_offset = struct.unpack_from("<I", data, offset + 4 + tile_index * 24 + 16)[0]
            if bit_offset == 0xFFFFFFFF:
                tile_offsets.append(None)
                continue
            if bit_offset < table_end or bit_offset + 2048 > len(data):
                raise ValueError("Foreground bit mask exceeds payload bounds")
            pixels = b"".join(unpack_bits[value] for value in data[bit_offset:bit_offset + 2048])
            tile = Image.frombytes("L", (128, 128), pixels)
            mask.paste(tile, ((tile_index % 5) * 128, (2 - tile_index // 5) * 128))
            tile_offsets.append(bit_offset)
        layers.append({
            "index": layer_index,
            "distance": distance,
            "clip_depth": depth if math.isfinite(depth) else None,
            "native_depth": "finite" if math.isfinite(depth) else "positive_infinity",
            "native_depth_bits": struct.unpack("<I", struct.pack("<f", depth))[0],
            "skip_reason": None if math.isfinite(depth) else "positive_infinity_has_no_finite_occlusion",
            "depth_override": _override(scene_id, background_index, layer_index, depth_patches) is not None,
            "tile_offsets": tile_offsets,
            "mask": mask,
        })
    return layers


def depth_map(layers: list[dict]) -> bytes:
    """Return row-major little-endian float32 depth: nearest mask, uncovered=1."""
    values = array("f", [1.0]) * (640 * 384)
    for layer in layers:
        mask = layer["mask"]
        depth = layer["clip_depth"]
        if mask.mode != "L" or mask.size != (640, 384):
            raise ValueError("Invalid foreground layer image or clip depth")
        if layer.get("skip_reason") == "positive_infinity_has_no_finite_occlusion":
            if (depth is not None or layer.get("native_depth") != "positive_infinity"
                    or layer.get("native_depth_bits") != 0x7f800000 or layer.get("distance") != 0):
                raise ValueError("Invalid nonfinite source foreground layer")
            # D3D8 pretransformed far depths may clip or clamp tofar1, depending
            # ondevicecaps. Neither contributes a finite depth to this min-map;
            # its RGB is the same originalJPEG already used by the background.
            continue
        if not isinstance(depth, (int, float)) or not math.isfinite(depth) or not 0 <= depth <= 1:
            raise ValueError("Invalid foreground layer image or clip depth")
        for index, covered in enumerate(mask.tobytes()):
            if covered and depth < values[index]:
                values[index] = depth
    if sys.byteorder != "little":
        values.byteswap()
    return values.tobytes()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("index", type=int)
    parser.add_argument("output", type=Path)
    parser.add_argument("--exe", type=Path, help="Original EvilDead.exe for complete scene depth corrections")
    args = parser.parse_args(argv)
    try:
        data = args.archive.read_bytes()
        entries = parse_archive(data)
        if not 0 <= args.index < len(entries):
            raise ValueError("Entry index is out of range")
        backgrounds = [entry for entry in entries if entry.resource_id >> 24 == 8 and entry.flags == 0]
        entry = entries[args.index]
        if entry not in backgrounds:
            raise ValueError("Selected entry is not a standalone background")
        payload = data[entry.offset:entry.offset + entry.size]
        image, name, tail_offset = decode_background(payload)
        scene_id = struct.unpack_from("<I", data)[0] & 0xFFFFFF00
        background_index = backgrounds.index(entry)
        executable = args.exe or args.archive.parent.parent / "EvilDead.exe"
        patches = read_depth_patches(executable.read_bytes()) if executable.is_file() else None
        layers = decode_masks(payload, tail_offset, scene_id=scene_id, background_index=background_index, depth_patches=patches)
        depths = depth_map(layers)
        args.output.mkdir(parents=True, exist_ok=False)
        metadata = {"source_name": name, "scene_id": hex(scene_id),
                    "background_index": background_index, "size": list(image.size),
                    "display_size": [640, 480], "depth_format": "little-endian float32, row-major; uncovered=1.0",
                    "layers": []}
        for layer in layers:
            filename = f"layer-{layer['index']}.png"
            layer["mask"].save(args.output / filename)
            metadata["layers"].append({key: value for key, value in layer.items() if key != "mask"}
                                      | {"mask_png": filename})
        (args.output / "depth.f32").write_bytes(depths)
        (args.output / "masks.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
        print(f"Saved {len(layers)} foreground masks and float32 depth map to {args.output}")
    except (OSError, ValueError, ImportError) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
