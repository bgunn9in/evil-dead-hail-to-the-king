"""Read PC resource containers without modifying the original game files."""

import argparse
from dataclasses import asdict, dataclass
from io import BytesIO
import json
from pathlib import Path
import struct


TABLE_OFFSET = 24
TABLE_CAPACITY = 1000
DATA_OFFSET = TABLE_OFFSET + TABLE_CAPACITY * 16
DC_DATA_OFFSET = 0x4000


@dataclass(frozen=True)
class Entry:
    index: int
    resource_id: int
    flags: int
    offset: int
    size: int


def _parse_archive(data: bytes, data_offset: int, platform: str) -> list[Entry]:
    if len(data) < data_offset:
        raise ValueError(f"Truncated {platform} container header/table")
    if struct.unpack_from("<I", data, 4)[0] not in (0x30002, 0x30004):
        raise ValueError(f"Unsupported {platform} container version")
    if struct.unpack_from("<I", data, 12)[0] != 20:
        raise ValueError(f"Unexpected {platform} container header size")
    count = struct.unpack_from("<I", data, 20)[0]
    if count > TABLE_CAPACITY:
        raise ValueError("Entry count exceeds table capacity")

    entries = []
    end = data_offset
    for index in range(count):
        resource_id, flags, offset, size = struct.unpack_from(
            "<4I", data, TABLE_OFFSET + index * 16
        )
        if offset != end or size > len(data) - offset:
            raise ValueError(f"Entry {index}: invalid offset/size")
        entries.append(Entry(index, resource_id, flags, offset, size))
        end = offset + size
    if end != len(data):
        raise ValueError("Unindexed bytes after the last entry")
    # Header length fields disagree with real files; trust checked entry ranges.
    return entries


def parse_archive(data: bytes) -> list[Entry]:
    """Read the original PC layout; Dreamcast is an explicit separate input."""
    return _parse_archive(data, DATA_OFFSET, "PC")


def parse_dc_archive(data: bytes) -> list[Entry]:
    """Read the checked Dreamcast directory, whose payload begins at 0x4000."""
    entries = _parse_archive(data, DC_DATA_OFFSET, "Dreamcast")
    if any(data[DATA_OFFSET:DC_DATA_OFFSET]):
        raise ValueError("Nonzero Dreamcast directory alignment padding")
    return entries


def decode_background(data: bytes):
    from PIL import Image

    if len(data) < 128 or b"\0" not in data[:128]:
        raise ValueError("Missing background source name")
    source_name = data[:128].split(b"\0", 1)[0].decode("ascii")
    image = Image.new("RGB", (640, 384))
    offset = 128
    for index in range(15):
        if offset + 4 > len(data):
            raise ValueError(f"Tile {index}: missing length")
        size = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        if not size or size % 4 or size > len(data) - offset:
            raise ValueError(f"Tile {index}: invalid length")
        with Image.open(BytesIO(data[offset:offset + size])) as tile:
            if tile.format != "JPEG" or tile.size != (128, 128):
                raise ValueError(f"Tile {index}: expected a 128x128 JPEG")
            image.paste(tile.convert("RGB"), ((index % 5) * 128, (2 - index // 5) * 128))
        offset += size
    # Additional scene data follows the tiles and is left undecoded.
    return image, source_name, offset


def extract_archive(data: bytes, entries: list[Entry], output: Path):
    names = [f"{entry.index:04d}_{entry.resource_id:08x}.bin" for entry in entries]
    if any((output / name).exists() for name in names):
        raise FileExistsError("Extraction would overwrite an existing file")
    output.mkdir(parents=True, exist_ok=True)
    for entry, name in zip(entries, names):
        with (output / name).open("xb") as stream:
            stream.write(data[entry.offset:entry.offset + entry.size])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("list", "extract", "background"):
        command = commands.add_parser(name)
        command.add_argument("archive", type=Path)
        if name == "background":
            command.add_argument("index", type=int)
        if name != "list":
            command.add_argument("output", type=Path)
    args = parser.parse_args(argv)

    try:
        data = args.archive.read_bytes()
        entries = parse_archive(data)
        if args.command == "list":
            print(json.dumps([asdict(entry) for entry in entries], indent=2))
        elif args.command == "extract":
            extract_archive(data, entries, args.output)
            print(f"Extracted {len(entries)} entries to {args.output}")
        else:
            if not 0 <= args.index < len(entries):
                raise ValueError("Entry index is out of range")
            entry = entries[args.index]
            if entry.resource_id >> 24 != 8 or entry.flags != 0:
                raise ValueError("Selected entry is not a standalone background")
            image, name, tail_offset = decode_background(
                data[entry.offset:entry.offset + entry.size]
            )
            with args.output.open("xb") as stream:
                image.save(stream, format="PNG")
            print(f"Saved {name} as {args.output} (640x384)")
            print(f"Undecoded tail: {entry.size - tail_offset} bytes")
    except (OSError, ValueError, ImportError) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
