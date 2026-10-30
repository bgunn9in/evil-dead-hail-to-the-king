"""Extract this game's MODE1/2352 GDI without changing the disc or its tracks."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import struct


SECTOR_BYTES = 2352
PAYLOAD_BYTES = 2048
SYNC = b'\0' + b'\xff' * 10 + b'\0'


class GdiImage:
    def __init__(self, path):
        self.path = Path(path).resolve(strict=True)
        rows = self.path.read_text(encoding='ascii').splitlines()
        self.tracks = []
        if not rows or not rows[0].isdigit() or not 1 <= int(rows[0]) <= 99:
            raise ValueError('Invalid GDI track count')
        for number, row in enumerate(rows[1:], 1):
            fields = shlex.split(row)
            if len(fields) != 6:
                raise ValueError('Invalid GDI track record')
            index, lba, control, sector_size = map(int, fields[:4])
            filename, offset = fields[4], int(fields[5])
            if index != number or lba < 0 or control not in (0, 4) or sector_size != SECTOR_BYTES or offset < 0:
                raise ValueError('Unsupported GDI track geometry')
            source = self.path.parent / filename
            track = source.resolve(strict=True)
            if (track.parent != self.path.parent or not track.is_file() or source.is_symlink()
                    or getattr(source.lstat(), 'st_file_attributes', 0) & 0x400):
                raise ValueError('GDI track must be an ordinary sibling file')
            length = track.stat().st_size - offset
            if length <= 0 or length % sector_size:
                raise ValueError('Invalid GDI track size')
            if self.tracks and lba < self.tracks[-1]['end']:
                raise ValueError('Overlapping GDI tracks')
            self.tracks.append({'number': index, 'lba': lba, 'end': lba + length // sector_size,
                                'control': control, 'path': track, 'offset': offset})
        if len(self.tracks) != int(rows[0]):
            raise ValueError('GDI track count mismatch')

    def read(self, lba, length):
        if lba < 0 or length < 0 or length > sum(t['end'] - t['lba'] for t in self.tracks) * PAYLOAD_BYTES:
            raise ValueError('Invalid disc range')
        result = bytearray()
        while len(result) < length:
            track = next((t for t in self.tracks if t['lba'] <= lba < t['end']), None)
            if track is None or track['control'] != 4:
                raise ValueError('ISO extent outside a data track')
            sectors = min(2048, track['end'] - lba, (length - len(result) + 2047) // 2048)
            with track['path'].open('rb') as stream:
                stream.seek(track['offset'] + (lba - track['lba']) * SECTOR_BYTES)
                raw = stream.read(sectors * SECTOR_BYTES)
            if len(raw) != sectors * SECTOR_BYTES:
                raise ValueError('Truncated data track')
            for begin in range(0, len(raw), SECTOR_BYTES):
                if raw[begin:begin + 12] != SYNC or raw[begin + 15] != 1:
                    raise ValueError('Unsupported raw sector header')
                result += raw[begin + 16:begin + 16 + PAYLOAD_BYTES]
            lba += sectors
        return bytes(result[:length])

    def volume(self):
        for track in reversed(self.tracks):
            if track['control'] != 4 or track['end'] - track['lba'] <= 16:
                continue
            pvd = self.read(track['lba'] + 16, PAYLOAD_BYTES)
            if pvd[:7] == b'\x01CD001\x01':
                if struct.unpack_from('<H', pvd, 128)[0] != PAYLOAD_BYTES:
                    raise ValueError('Unsupported ISO logical block size')
                return track, pvd
        raise ValueError('No ISO9660 primary volume descriptor')


def directory_records(data):
    cursor = 0
    while cursor < len(data):
        size = data[cursor]
        if not size:
            cursor = (cursor // PAYLOAD_BYTES + 1) * PAYLOAD_BYTES
            continue
        if size < 34 or cursor + size > len(data) or cursor % PAYLOAD_BYTES + size > PAYLOAD_BYTES:
            raise ValueError('Invalid ISO directory record length')
        record = data[cursor:cursor + size]
        cursor += size
        extent, length = struct.unpack_from('<I', record, 2)[0], struct.unpack_from('<I', record, 10)[0]
        if extent != struct.unpack_from('>I', record, 6)[0] or length != struct.unpack_from('>I', record, 14)[0]:
            raise ValueError('ISO little/big endian range mismatch')
        name_size = record[32]
        if record[1] or record[25] & ~3 or record[26] or record[27] or not name_size or 33 + name_size > size:
            raise ValueError('Unsupported ISO directory attributes')
        name = record[33:33 + name_size]
        if name in (b'\0', b'\1'):
            continue
        name = name.decode('ascii')
        if name.endswith(';1'):
            name = name[:-2]
        if not re.fullmatch(r'[A-Za-z0-9_ .-]+', name) or name in ('.', '..') or name.endswith((' ', '.')):
            raise ValueError('Unsafe or unsupported ISO filename')
        yield {'name': name, 'lba': extent, 'bytes': length, 'directory': bool(record[25] & 2)}


def extract(gdi, output):
    image = GdiImage(gdi)
    track, pvd = image.volume()
    root_record = pvd[156:190]
    root_lba = struct.unpack_from('<I', root_record, 2)[0]
    root_size = struct.unpack_from('<I', root_record, 10)[0]
    output = Path(output).resolve()
    if output.exists() or output == image.path.parent or image.path.parent.is_relative_to(output):
        raise FileExistsError('Extraction requires a new output directory separate from tracks')
    entries, directories, seen = [], [(Path(), root_lba, root_size)], set()
    for relative, lba, size in directories:
        if not 0 < size <= 16 * 1024 * 1024 or len(directories) > 4096:
            raise ValueError('Excessive ISO directory size/count')
        for record in directory_records(image.read(lba, size)):
            name = relative / record['name']
            key = name.as_posix().casefold()
            if key in seen:
                raise ValueError('Duplicate ISO output path')
            seen.add(key)
            if record['directory']:
                if len(name.parts) > 16:
                    raise ValueError('Excessive ISO directory depth')
                directories.append((name, record['lba'], record['bytes']))
            else:
                entries.append({**record, 'path': name.as_posix()})
    output.mkdir(parents=True, exist_ok=False)
    for entry in entries:
        data = image.read(entry['lba'], entry['bytes'])
        target = output / entry['path']
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('xb') as stream:
            stream.write(data)
        entry['sha256'] = hashlib.sha256(data).hexdigest()
        entry['head_hex'] = data[:32].hex()
        del entry['name'], entry['directory']
    boot = image.read(track['lba'], 16 * PAYLOAD_BYTES)
    manifest = {'format': 'EDDREAMCASTISO1', 'gdi': str(image.path),
                'gdi_sha256': hashlib.sha256(image.path.read_bytes()).hexdigest(),
                'volume_id': pvd[40:72].decode('ascii').strip(), 'data_track': track['number'],
                'boot_header': boot[:256].decode('ascii', errors='backslashreplace'),
                'files': entries, 'file_count': len(entries), 'total_bytes': sum(e['bytes'] for e in entries)}
    with (output.parent / (output.name + '-manifest.json')).open('x', encoding='utf-8') as stream:
        json.dump(manifest, stream, indent=2)
        stream.write('\n')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('gdi', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = extract(args.gdi, args.output)
    print(json.dumps({k: result[k] for k in ('volume_id', 'file_count', 'total_bytes')}))
