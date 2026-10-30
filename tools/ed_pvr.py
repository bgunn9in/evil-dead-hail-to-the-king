"""Decode the uncompressed PVRT formats present on the original Dreamcast disc."""

import argparse
from pathlib import Path
import struct

from PIL import Image


def spread_bits(value):
    result, bit = 0, 0
    while value:
        result |= (value & 1) << (bit * 2)
        value >>= 1
        bit += 1
    return result


def _twiddled_index(x, y, width, height):
    side = min(width, height)
    block = (x // side + y // side) * side * side
    return block + (spread_bits(x % side) << 1) + spread_bits(y % side)


def _yuv(y, u, v):
    # PowerVR YUV422 full-range conversion; integer divisions truncate to zero.
    u, v = u - 128, v - 128
    clamp = lambda n: min(255, max(0, n))
    return (clamp(y + int(v * 11 / 8)), clamp(y - int((u * 11 + v * 22) / 32)),
            clamp(y + int(u * 110 / 64)), 255)


def decode_pvr(data):
    if len(data) < 16:
        raise ValueError('Truncated PVR header')
    magic, size, pixel, layout, reserved, width, height = struct.unpack_from('<4sIBBHHH', data)
    if magic != b'PVRT' or size != len(data) - 8 or reserved:
        raise ValueError('Invalid PVR header/size')
    if (pixel, layout) not in ((0, 1), (2, 1), (3, 1), (3, 9), (0, 13), (2, 13)):
        raise ValueError('Unsupported PVR pixel/storage format')
    if (not 1 <= width <= 1024 or not 1 <= height <= 1024
            or width & (width - 1) or height & (height - 1)
            or (layout == 1 and width != height) or (pixel == 3 and width % 2)):
        raise ValueError('Unsupported PVR dimensions')
    if len(data) != 16 + width * height * 2:
        raise ValueError('Invalid PVR pixel data size')
    values = struct.unpack_from(f'<{width * height}H', data, 16)
    rgba = bytearray()
    for y in range(height):
        for x in range(width):
            index = y * width + x if layout == 9 else _twiddled_index(x, y, width, height)
            value = values[index]
            if pixel == 3:
                pair_x = x & ~1
                first = y * width + pair_x if layout == 9 else _twiddled_index(pair_x, y, width, height)
                second = first + 1 if layout == 9 else _twiddled_index(pair_x + 1, y, width, height)
                rgba.extend(_yuv(value >> 8, values[first] & 255, values[second] & 255))
            elif pixel == 2:
                rgba.extend(((value >> 8 & 15) * 17, (value >> 4 & 15) * 17, (value & 15) * 17, (value >> 12) * 17))
            else:
                rgba.extend(((value >> 10 & 31) * 255 // 31, (value >> 5 & 31) * 255 // 31,
                             (value & 31) * 255 // 31, 255 if value & 0x8000 else 0))
    return Image.frombytes('RGBA', (width, height), bytes(rgba))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError('PVR conversion requires a new output file')
    decode_pvr(args.input.read_bytes()).save(args.output)
