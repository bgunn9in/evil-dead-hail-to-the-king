"""Create a fresh lossless PNG/FLAC storage derivative of a sealed DC profile."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from io import BytesIO
import os
from pathlib import Path
import shutil
import struct
import subprocess

from PIL import Image
import import_dreamcast_runtime as imp

FORMAT = 'EDDREAMCAST-LOSSLESS1'
BASE = 'provenance/lossless-base.json'
OVERLAY = 'provenance/lossless-storage.json'


def immutable_copy(source, target):
    try:
        os.link(source, target)
    except OSError as error:
        if getattr(error, 'winerror', None) != 1142:
            raise
        # NTFS caps one file at1024 hardlinks; keep the fallback exclusive too.
        with source.open('rb') as original, target.open('xb') as copied:
            shutil.copyfileobj(original, copied)


def kind(name):
    path = Path(name)
    if path.suffix == '.bmp' and path.name != 'font.bmp':
        return 'bmp24-png'
    if path.suffix == '.depth':
        return 'float32-png'
    if path.suffix == '.coverage8':
        return 'coverage8-png'
    if path.suffix == '.wav' and '/audio/streams/' in '/' + name:
        return 'pcm16-flac'
    return None


def stored_name(name, codec):
    return str(Path(name).with_suffix('.png' if codec == 'bmp24-png' else '.flac')).replace('\\', '/') if codec in ('bmp24-png', 'pcm16-flac') else name + '.png'


def bmp_shape(header):
    if len(header) < 54 or header[:2] != b'BM' or struct.unpack_from('<I', header, 10)[0] != len(header):
        raise ValueError('Invalid lossless BMP header')
    width, height, planes, bits, compression = struct.unpack_from('<iiHHI', header, 18)
    if not 0 < width <= 4096 or not 0 < abs(height) <= 4096 or planes != 1 or bits != 24 or compression:
        raise ValueError('Expected bounded uncompressed24 BMP')
    return width, height, (width * 3 + 3) & ~3


def decode(path, change):
    codec = change['codec']
    if codec == 'pcm16-flac':
        raw = path.read_bytes()
        if len(raw) < 42 or raw[:4] != b'fLaC' or raw[4] & 127 or int.from_bytes(raw[5:8], 'big') != 34:
            raise ValueError('Invalid native FLAC streaminfo')
        info = int.from_bytes(raw[18:26], 'big')
        frames = info & ((1 << 36) - 1)
        if info >> 44 != 44100 or (info >> 41 & 7) != 1 or (info >> 36 & 31) != 15 or frames * 4 + 44 != change['source']['bytes']:
            raise ValueError('FLAC changed source format/frame count')
        process = subprocess.run(['ffmpeg', '-v', 'error', '-nostdin', '-i', str(path), '-fs', str(frames * 4 + 8192),
                                  '-f', 's16le', '-'], capture_output=True, timeout=60)
        if process.returncode or len(process.stdout) != frames * 4:
            raise ValueError('Cannot decode exact source FLAC PCM')
        return bytes.fromhex(change['header']) + process.stdout
    with Image.open(path) as image:
        image.load()
        if codec == 'bmp24-png':
            header = bytes.fromhex(change['header'])
            width, height, stride = bmp_shape(header)
            if image.mode != 'RGB' or image.size != (width, abs(height)):
                raise ValueError('PNG changed source bitmap shape/precision')
            return header + image.tobytes('raw', 'BGR', stride, -1 if height > 0 else 1)
        shape, mode = ((320, 240), 'RGBA') if codec == 'float32-png' else ((256, 256), 'L')
        if image.size != shape or image.mode != mode:
            raise ValueError('PNG changed packed pixel representation')
        return image.tobytes()


def verify_derivation(assets, manifest):
    receipt = manifest['lossless_storage']
    if receipt.get('format') != FORMAT or receipt.get('base_manifest_path') != BASE or receipt.get('overlay_manifest_path') != OVERLAY:
        raise ValueError('Unknown lossless storage derivation')
    raw, overlay_raw = (assets / BASE).read_bytes(), (assets / OVERLAY).read_bytes()
    if imp.digest(raw) != receipt['base_manifest_sha256'] or imp.digest(overlay_raw) != receipt['overlay_manifest_sha256']:
        raise ValueError('Lossless storage provenance differs')
    base, overlay = imp.load(assets / BASE), imp.load(assets / OVERLAY)
    if ('lossless_storage' in base or base.get('format') != imp.FORMAT or base.get('status') != 'verified-import' or
            base['inputs_manifest_sha256'] != manifest['inputs_manifest_sha256'] or base['source_manifest_sha256'] != imp.SOURCE_SHA or
            imp.digest(imp.json_bytes(base['files'])) != base['files_sha256'] or overlay.get('format') != FORMAT):
        raise ValueError('Invalid lossless base identity')
    before, after = {r['path']: r for r in base['files']}, {r['path']: r for r in manifest['files']}
    if len(before) != len(base['files']) or base['file_count'] != len(before):
        raise ValueError('Duplicate or incomplete lossless base file table')
    sources, stored = set(), set()
    seen = {}
    for change in overlay['changes']:
        source, target, codec = change['source'], change['stored'], change['codec']
        name, output = source['path'], target['path']
        imp.relative(name);imp.relative(output)
        if (name in sources or output in stored or codec != kind(name) or output != stored_name(name, codec) or
                source != before.get(name) or target != after.get(output) or target['bytes'] >= source['bytes']):
            raise ValueError('Invalid lossless change table')
        sources.add(name);stored.add(output)
        imp.no_reparse(assets / output)
        key = (codec, target['sha256'], change.get('header', ''))
        if key not in seen:
            data = (assets / output).read_bytes()
            if len(data) != target['bytes'] or imp.digest(data) != target['sha256']:
                raise ValueError('Lossless encoded bytes differ')
            decoded = decode(assets / output, change)
            seen[key] = (len(decoded), imp.digest(decoded))
        if seen[key] != (source['bytes'], source['sha256']):
            raise ValueError('Lossless resource did not reconstruct original bytes: ' + name)
    if len(after) != len(manifest['files']) or set(after) != (set(before) - sources) | stored | {BASE, OVERLAY}:
        raise ValueError('Lossless file identities differ')
    for name in set(before) - sources:
        if before[name] != after[name]:
            raise ValueError('Unrelated resource changed: ' + name)
    return base


def convert(source, assets, row, codec):
    target = assets / imp.relative(stored_name(row['path'], codec))
    raw = (source / row['path']).read_bytes()
    if len(raw) != row['bytes'] or imp.digest(raw) != row['sha256']:
        raise ValueError('Source changed during lossless conversion')
    change = {'source': row, 'codec': codec}
    if codec == 'pcm16-flac':
        if raw[:4] != b'RIFF' or raw[8:16] != b'WAVEfmt ' or struct.unpack_from('<I', raw, 16)[0] != 16 or raw[36:40] != b'data' or struct.unpack_from('<I', raw, 40)[0] + 44 != len(raw):
            raise ValueError('Expected canonical source music WAV')
        change['header'] = raw[:44].hex()
        process = subprocess.run(['ffmpeg', '-v', 'error', '-nostdin', '-n', '-i', str(source / row['path']), '-map_metadata', '-1',
                                  '-c:a', 'flac', '-compression_level', '8', '-threads', '1', str(target)], capture_output=True, timeout=120)
        if process.returncode:
            raise ValueError('FLAC conversion failed: ' + process.stderr.decode('utf-8', 'replace'))
    else:
        if codec == 'bmp24-png':
            offset = struct.unpack_from('<I', raw, 10)[0]
            header = raw[:offset];width, height, stride = bmp_shape(header)
            image = Image.frombytes('RGB', (width, abs(height)), raw[offset:], 'raw', 'BGR', stride, -1 if height > 0 else 1)
            change['header'] = header.hex()
        else:
            image = Image.frombytes('RGBA' if codec == 'float32-png' else 'L', (320, 240) if codec == 'float32-png' else (256, 256), raw)
        image.save(target, format='PNG', compress_level=9)
    encoded = target.read_bytes()
    if decode(target, change) != raw:
        raise ValueError('Lossless round-trip differs: ' + row['path'])
    if len(encoded) >= len(raw):
        target.unlink()
        return None
    change['stored'] = {'path': target.relative_to(assets).as_posix(), 'bytes': len(encoded), 'sha256': imp.digest(encoded)}
    return change


def derive(source, output, expected_source):
    source = imp.checked_path(source)
    output = imp.output_guard(output, (source,))
    raw = (source / imp.MARKER).read_bytes();base = imp.load(source / imp.MARKER)
    if imp.digest(raw) != expected_source or 'lossless_storage' in base:
        raise ValueError('Unexpected immutable source profile')
    imp.verify(source / 'provenance/import-inputs.json', source.parent)
    output.mkdir(parents=True);(output / 'INCOMPLETE').write_text('Lossless storage conversion pending\n', encoding='ascii')
    assets = output / 'assets';assets.mkdir()
    records, groups = {}, {}
    for row in base['files']:
        target = assets / imp.relative(row['path']);target.parent.mkdir(parents=True, exist_ok=True)
        immutable_copy(source / row['path'], target);records[row['path']] = row
        codec = kind(row['path'])
        if codec:groups.setdefault((codec, row['sha256']), []).append(row)
    changes = []
    with ThreadPoolExecutor(max_workers=4) as pool:
        jobs = {pool.submit(convert, source, assets, rows[0], codec): rows for (codec, _), rows in groups.items()}
        for index, job in enumerate(as_completed(jobs)):
            change = job.result()
            if change:
                encoded = assets / change['stored']['path']
                for row in jobs[job]:
                    stored = {**change['stored'], 'path': stored_name(row['path'], change['codec'])}
                    if stored['path'] != change['stored']['path']:
                        if change['codec'] == 'float32-png':
                            # Thousands of tiny depth maps must leave NTFS link slots for package/test clones.
                            with encoded.open('rb') as original, (assets / stored['path']).open('xb') as copied:
                                shutil.copyfileobj(original, copied)
                        else:
                            immutable_copy(encoded, assets / stored['path'])
                    (assets / row['path']).unlink()
                    records.pop(row['path']);records[stored['path']] = stored
                    changes.append({**change, 'source': row, 'stored': stored})
            if index % 250 == 0:print(f'Converted {index+1}/{len(jobs)} unique resources', flush=True)
    overlay = {'format': FORMAT, 'changes': sorted(changes, key=lambda row: row['source']['path'])}
    overlay_raw = imp.json_bytes(overlay)
    for name, data in ((BASE, raw), (OVERLAY, overlay_raw)):
        (assets / name).write_bytes(data);records[name] = {'path': name, 'bytes': len(data), 'sha256': imp.digest(data)}
    rows = [records[name] for name in sorted(records)]
    manifest = dict(base, files=rows, file_count=len(rows), files_sha256=imp.digest(imp.json_bytes(rows)),
                    lossless_storage={'format': FORMAT, 'base_manifest_path': BASE, 'base_manifest_sha256': expected_source,
                                      'overlay_manifest_path': OVERLAY, 'overlay_manifest_sha256': imp.digest(overlay_raw)},
                    seal_scope='Original R17 profile reconstructed exactly from PNG/FLAC; fonts, scripts, meshes and other resources unchanged.')
    physical = {}
    for row in rows:
        info = (assets / row['path']).stat()
        physical[info.st_dev, info.st_ino] = info.st_size
    manifest['hashed_physical_bytes'] = sum(physical.values())
    verify_derivation(assets, manifest)
    (assets / imp.MARKER).write_bytes(imp.json_bytes(manifest));(output / 'INCOMPLETE').unlink()
    imp.verify(assets / 'provenance/import-inputs.json', output)
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source-sha256', required=True)
    args = parser.parse_args()
    result = derive(args.source, args.output, args.source_sha256)
    print('Lossless profile sealed:', result['file_count'], 'files', flush=True)
