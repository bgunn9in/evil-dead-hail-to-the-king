"""Build a fresh 2x visual derivative of a sealed Dreamcast profile.

Original pixels, scripts and audio are never overwritten. The working profile
cannot be selected or packaged until compressed movies and its new seal exist.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

from PIL import Image
import import_dreamcast_runtime as imp

FORMAT = 'ED-DREAMCAST-UPSCALE1'
BASE_RECORD = 'provenance/upscale-native-base.json'
NATIVE_PIXEL_FILES = frozenset(
    prefix + name
    for prefix, menu in (('', 'aa01'), ('levelB/', 'bb01'))
    for name in ('font.bmp', 'font.txt', 'gui/textures/0b942034.rgba',
                 'gui/textures/0b942054.rgba', menu + '/main-menu.texture', 'vfx/atlas.rgba'))


def replace_bytes(path, data):
    # The initial working tree uses immutable hardlinks; detach before writing.
    path.unlink()
    path.write_bytes(data)


def bmp(path):
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b'BM':
        raise ValueError('Invalid source BMP')
    offset = struct.unpack_from('<I', data, 10)[0]
    width, height, planes, bits, compression = struct.unpack_from('<iiHHI', data, 18)
    if width <= 0 or not height or planes != 1 or bits not in (24, 32) or compression:
        raise ValueError('Unsupported bounded source BMP')
    stride = (width * bits // 8 + 3) & ~3
    if len(data) != offset + abs(height) * stride:
        raise ValueError('Source BMP pixel length differs')
    return Image.frombytes('RGBA' if bits == 32 else 'RGB', (width, abs(height)),
                           data[offset:], 'raw', 'BGRA' if bits == 32 else 'BGR',
                           stride, -1 if height > 0 else 1)


def rgba_texture(data):
    if len(data) < 20 or data[:8] != b'EDTEX1\0\0':
        raise ValueError('Expected EDTEX1 pixels')
    identity, width, height = struct.unpack_from('<III', data, 8)
    if not 0 < width <= 2048 or not 0 < height <= 2048 or len(data) != 20 + width * height * 4:
        raise ValueError('EDTEX1 dimensions/length differ')
    return identity, Image.frombytes('RGBA', (width, height), data[20:])


def soft_rgba(image):
    # Filter premultiplied pixels so transparent borders cannot turn dark.
    return image.convert('RGBa').resize((image.width * 2, image.height * 2),
                                      Image.Resampling.BILINEAR).convert('RGBA')


def inventory_dimensions(data):
    data = bytearray(data)
    if data[:8] != b'EDINV2\0\0' or struct.unpack_from('<I', data, 8)[0] != 1251:
        raise ValueError('Expected native localized inventory')
    count = struct.unpack_from('<I', data, 12)[0]
    if not 0 < count <= 8 or len(data) < 20 + count * 12:
        raise ValueError('Invalid inventory texture table')
    result = {}
    for index in range(count):
        offset = 20 + index * 12
        identity, width, height = struct.unpack_from('<III', data, offset)
        if (width, height) != (256, 512) or identity in result:
            raise ValueError('Unexpected native inventory atlas')
        result[identity] = (width, height)
        struct.pack_into('<II', data, offset + 4, width * 2, height * 2)
    return bytes(data), result


def wrapped(image, border=16):
    canvas = Image.new('RGB', (image.width + border * 2, image.height + border * 2))
    for y in range(-image.height, canvas.height, image.height):
        for x in range(-image.width, canvas.width, image.width):
            canvas.paste(image, (x + border, y + border))
    return canvas


def prepare(inputs, source, output, executable):
    source = imp.checked_path(source)
    output = imp.output_guard(output, [source])
    imp.verify(inputs, source.parent)
    original = imp.load(source / imp.MARKER)
    if 'visual_upscale' in original:
        raise ValueError('Use native pixels, never upscale an existing upscale')
    executable = imp.checked_path(executable)
    models = executable.parent / 'models-upconv_7_photo'
    for name in ('scale2.0x_model.bin', 'scale2.0x_model.param'):
        imp.checked_path(models / name)
    output.mkdir(parents=True)
    (output / 'INCOMPLETE').write_text('2x Dreamcast derivative in progress.\n', encoding='ascii')
    assets = output / 'assets'
    assets.mkdir()
    for row in original['files']:
        target = assets / imp.relative(row['path'])
        target.parent.mkdir(parents=True, exist_ok=True)
        os.link(source / row['path'], target)
    (assets / BASE_RECORD).write_bytes((source / imp.MARKER).read_bytes())
    changed = {}
    records = {r['path']: r for r in original['files']}
    pending = {}
    input_dir, neural_dir = output / 'neural-input', output / 'neural-output'
    input_dir.mkdir(); neural_dir.mkdir()

    def note(name, method, size=None):
        changed[name] = dict(path=name, source_sha256=records[name]['sha256'], method=method)
        if size:
            changed[name].update(input_dimensions=list(size), output_dimensions=[n * 2 for n in size])

    def neural(name, image, texture):
        key = records[name]['sha256'] + ('-wrap' if texture else '-edge')
        if key not in pending:
            (wrapped(image) if texture else image).save(input_dir / (key + '.png'))
            pending[key] = []
        pending[key].append((name, image.size, texture))
        note(name, 'photo2x-no-denoise' + ('-wrapped-border16' if texture else ''), image.size)

    for name in sorted(records):
        # Native text strokes and tightly packed VFX cells must not be filtered.
        if name in NATIVE_PIXEL_FILES:
            continue
        path = source / name
        target = assets / name
        if name.endswith('/background_size.txt'):
            if path.read_bytes().split() != [b'EDPROFILEBG1', b'320', b'240', b'0.875']:
                raise ValueError('Unknown original background profile')
            replace_bytes(target, b'EDPROFILEBG1\n640 480 0.875\n')
            note(name, 'physical-colour-only2x-native-depth-and-mask')
        elif path.name == 'inventory.bin':
            data, atlases = inventory_dimensions(path.read_bytes())
            replace_bytes(target, data); note(name, 'physical-atlas-dimensions-only2x')
            for identity, size in atlases.items():
                atlas_name = path.with_name(f'inventory_{identity:08x}.rgba').relative_to(source).as_posix()
                raw = (source / atlas_name).read_bytes()
                if len(raw) != size[0] * size[1] * 4:
                    raise ValueError('Inventory atlas byte length differs')
                image = Image.frombytes('RGBA', size, raw)
                replace_bytes(assets / atlas_name, soft_rgba(image).tobytes())
                note(atlas_name, 'premultiplied-bilinear2x', size)
        elif path.suffix == '.bmp':
            image = bmp(path)
            if image.mode == 'RGBA' and image.getchannel('A').getextrema() == (255, 255):
                image = image.convert('RGB')
            if image.mode != 'RGB':
                raise ValueError('Unexpected alpha BMP outside font')
            neural(name, image, not path.name.startswith('background_'))
        elif path.suffix in ('.rgba', '.texture') and not path.name.startswith('inventory_'):
            identity, image = rgba_texture(path.read_bytes())
            is_atlas = '/gui/' in '/' + name or '/vfx/' in '/' + name or path.suffix == '.texture'
            if not is_atlas and image.getchannel('A').getextrema() == (255, 255):
                neural(name, image.convert('RGB'), True)
            else:
                result = soft_rgba(image)
                replace_bytes(target, struct.pack('<8sIII', b'EDTEX1\0\0', identity, result.width, result.height) + result.tobytes())
                note(name, 'premultiplied-bilinear2x-preserved-atlas-layout', image.size)

    command = [str(executable), '-i', str(input_dir), '-o', str(neural_dir), '-m', str(models),
               '-n', '-1', '-s', '2', '-t', '400', '-j', '2:1:2', '-f', 'png']
    print(f'Neural photo2x: {len(pending)} unique inputs, {sum(len(x) for x in pending.values())} files', flush=True)
    with (output / 'neural.log').open('w', encoding='utf-8') as log:
        subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
    for key, destinations in pending.items():
        with Image.open(neural_dir / (key + '.png')) as encoded:
            for name, size, texture in destinations:
                image = encoded.convert('RGB')
                if texture:
                    image = image.crop((32, 32, 32 + size[0] * 2, 32 + size[1] * 2))
                if image.size != (size[0] * 2, size[1] * 2):
                    raise ValueError('Neural output dimensions differ')
                target = assets / name
                if target.suffix == '.bmp':
                    target.unlink(); image.save(target)
                else:
                    identity, _ = rgba_texture((source / name).read_bytes())
                    replace_bytes(target, struct.pack('<8sIII', b'EDTEX1\0\0', identity, image.width, image.height) + image.convert('RGBA').tobytes())
    hashes = imp.Hashes()
    receipt = dict(format=FORMAT, scale=2, base_manifest_sha256=imp.digest((source / imp.MARKER).read_bytes()),
                   base_manifest_path=BASE_RECORD, source_manifest_sha256=original['source_manifest_sha256'],
                   algorithm=dict(tool='waifu2x-ncnn-vulkan20250915', executable_sha256=hashes.file(executable),
                                  model='upconv_7_photo', noise=-1, tile=400, scale=2,
                                  model_files={p.name: hashes.file(p) for p in sorted(models.glob('scale2.0x_model.*'))},
                                  url='https://github.com/nihui/waifu2x-ncnn-vulkan/releases/tag/20250915'),
                   changed_files=[changed[n] for n in sorted(changed)])
    (output / 'upscale-stills.json').write_bytes(imp.json_bytes(receipt))
    print(f'Stills complete: {len(changed)} derived files; movies/seal pending.', flush=True)
    return receipt


def allowed_change(name):
    p = Path(name)
    return (p.suffix in ('.bmp', '.texture', '.rgba') or p.name in ('font.txt', 'inventory.bin', 'background_size.txt') or
            (name.startswith(('movies/', 'levelB/movies/')) and p.suffix in ('.edm', '.json')))


def verify_derivation(assets, manifest):
    derivation = manifest['visual_upscale']
    if derivation.get('format') != FORMAT or derivation.get('scale') != 2 or derivation.get('base_manifest_path') != BASE_RECORD:
        raise ValueError('Unknown Dreamcast visual derivation')
    raw = (assets / BASE_RECORD).read_bytes()
    if imp.digest(raw) != derivation['base_manifest_sha256']:
        raise ValueError('Native base seal identity differs')
    base = json.loads(raw)
    if (base.get('format') != imp.FORMAT or base.get('status') != 'verified-import' or 'visual_upscale' in base or
            base['source_manifest_sha256'] != imp.SOURCE_SHA or base['inputs_manifest_sha256'] != manifest['inputs_manifest_sha256'] or
            imp.digest(imp.json_bytes(base['files'])) != base['files_sha256']):
        raise ValueError('Invalid original native base seal')
    before = {r['path']: r for r in base['files']}
    after = {r['path']: r for r in manifest['files']}
    if set(after) != set(before) | {BASE_RECORD}:
        raise ValueError('Visual derivative changed original resource identities')
    changes = derivation['changed_files']
    changed = {r['path']: r for r in changes}
    actual = {name for name in before if before[name] != after[name]}
    if len(changed) != len(changes) or set(changed) != actual:
        raise ValueError('Visual derivative change table differs')
    for name, row in changed.items():
        if not allowed_change(name) or row['source_sha256'] != before[name]['sha256'] or row['sha256'] != after[name]['sha256']:
            raise ValueError('Nonvisual or unproven derivative change: ' + name)


def validate_movies(native, movies, rows, hashes):
    original = imp.load(native / 'manifest.json')
    before = {row['id']: row for row in original['movies']}
    if len(rows) != 56 or {row['id'] for row in rows} != set(before):
        raise ValueError('Compressed movie identities differ from the original')
    header = struct.Struct('<8s10I2Q')
    audio_total = frames_total = 0
    for row in rows:
        stem = f'{row["id"]:08x}'
        old, new = native / (stem + '.edm'), movies / (stem + '.edm')
        imp.no_reparse(new)
        if (imp.load(movies / (stem + '.json')) != row or
                row['input_edm_sha256'] != before[row['id']]['decoded_sha256'] or
                row['source_name'] != before[row['id']]['source_name'] or
                row['source_sha256'] != before[row['id']]['source_sha256'] or
                hashes.file(new) != row['decoded_sha256'] or new.stat().st_size != row['decoded_bytes']):
            raise ValueError('Compressed movie source/output identity differs')
        with old.open('rb') as a, new.open('rb') as b:
            ah, bh = header.unpack(a.read(64)), header.unpack(b.read(64))
            if (ah[0] != b'EDMOVIE1' or bh[0] != b'EDMOVIE2' or
                    bh[1:3] != (ah[1] * 2, ah[2] * 2) or ah[3:9] != bh[3:9] or
                    bh[9:11] != (ah[9] * 2, ah[10] * 2) or ah[12] != bh[12] or
                    new.stat().st_size != 64 + bh[11] + bh[12]):
                raise ValueError('Movie timing/audio/dimensions changed')
            a.seek(64 + ah[11]); b.seek(64 + bh[11]); pcm = hashlib.sha256()
            remaining = ah[12]
            while remaining:
                size = min(remaining, 1024 * 1024)
                block = a.read(size)
                if len(block) != size or block != b.read(size):
                    raise ValueError('Original movie PCM changed')
                pcm.update(block); remaining -= size
            if pcm.hexdigest() != row['audio_pcm_sha256']:
                raise ValueError('Movie PCM identity differs')
            frames_total += ah[3]; audio_total += ah[12]
    return dict(unique=56, original_frames=frames_total, exact_PCM_bytes=audio_total)


def seal(inputs, output, movies):
    output, movies = imp.checked_path(output), imp.checked_path(movies)
    if not (output / 'INCOMPLETE').is_file():
        raise ValueError('Only an incomplete fresh working derivative can be sealed')
    assets = output / 'assets'
    receipt = imp.load(output / 'upscale-stills.json')
    base = imp.load(assets / BASE_RECORD)
    movie_manifest = imp.load(movies / 'manifest.json')
    if movie_manifest.get('format') != 'EDMOVIE2' or len(movie_manifest['movies']) != 56:
        raise ValueError('All56 checked compressed 2x Dreamcast movies required')
    if movie_manifest['source_manifest_sha256'] != imp.digest((assets / 'movies/manifest.json').read_bytes()):
        raise ValueError('Compressed movies belong to a different native source')
    hashes = imp.Hashes()
    movie_check = validate_movies(assets / 'movies', movies, movie_manifest['movies'], hashes)
    before = {r['path']: r for r in base['files']}
    changes = {r['path']: r for r in receipt['changed_files']}
    for prefix in ('movies/', 'levelB/movies/'):
        for name in sorted(n for n in before if n.startswith(prefix)):
            source = movies / name[len(prefix):]
            if not source.is_file():
                raise ValueError('Missing compressed movie sidecar: ' + str(source))
            target = assets / name
            target.unlink(); os.link(source, target)
            changes[name] = dict(path=name, source_sha256=before[name]['sha256'], method='AV1-2x-original-FPS-exact-PCM')
    records = []
    for path in sorted(assets.rglob('*')):
        imp.no_reparse(path)
        if path.is_file():
            name = path.relative_to(assets).as_posix()
            row = dict(path=name, bytes=path.stat().st_size, sha256=hashes.file(path))
            records.append(row)
            if name in changes:
                changes[name]['sha256'] = row['sha256']
    receipt['changed_files'] = [changes[n] for n in sorted(changes) if changes[n]['sha256'] != changes[n]['source_sha256']]
    receipt['movies'] = dict(manifest_sha256=hashes.file(movies / 'manifest.json'), codec='AV1', **movie_check)
    manifest = dict(base, files=records, file_count=len(records), files_sha256=imp.digest(imp.json_bytes(records)),
                    visual_upscale=receipt, seal_scope='Native Dreamcast base + explicit2x visual derivatives; all other bytes preserved.',
                    hashed_physical_bytes=hashes.bytes)
    verify_derivation(assets, manifest)
    (assets / imp.MARKER).write_bytes(imp.json_bytes(manifest))
    (output / 'INCOMPLETE').unlink()
    try:
        imp.verify(inputs, output)
    except Exception:
        (output / 'INCOMPLETE').write_text('Final derivative verification failed; do not select or package.\n', encoding='ascii')
        raise
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs', type=Path, default=imp.ROOT / 'research/dreamcast-runtime-native-style-inputs-20261029.json')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--waifu', type=Path)
    parser.add_argument('--movies', type=Path)
    args = parser.parse_args()
    if args.movies:
        seal(args.inputs, args.output, args.movies)
    elif args.source and args.waifu:
        prepare(args.inputs, args.source, args.output, args.waifu)
    else:
        parser.error('Use --source/--waifu to prepare, or --movies to finish and seal')
