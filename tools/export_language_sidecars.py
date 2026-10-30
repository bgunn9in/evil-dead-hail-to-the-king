"""Fresh original-English UI sidecars; never replace a sealed Dreamcast profile."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import import_dreamcast_runtime as imp
from ed_dc_model import decode_texture
from ed_objects import encode_texture
from export_dreamcast_ui import Resources, read_inventory
from compile_localization import inventory_label


def sha(data):
    return hashlib.sha256(data).hexdigest()


def english_inventory(original, native):
    """Retain active atlas/geometry metadata and replace only source-authored labels."""
    if len(original) < 20 or original[:8] != b'EDINV2\0\0':
        raise ValueError('Expected declared native CP1251 inventory')
    encoding, textures, count = struct.unpack_from('<3I', original, 8)
    if encoding != 1251 or not 0 < textures <= 8 or count != len(native['items']):
        raise ValueError('Inventory language key count differs')
    at = 20 + textures * 12
    if at > len(original):
        raise ValueError('Truncated inventory texture table')
    result = bytearray(original[:at])
    seen = set()
    source = {item['item_id']: item for item in native['items']}
    texture_ids = [struct.unpack_from('<I', original, 20 + n * 12)[0] for n in range(textures)]
    for _ in range(count):
        if at + 44 > len(original):
            raise ValueError('Truncated inventory record')
        prefix = original[at:at + 36]
        identity, flags, atlas, shape = struct.unpack_from('<4I', prefix)
        name_size, description_size = struct.unpack_from('<2I', original, at + 36)
        at += 44
        if not 0 < name_size <= 256 or not 0 < description_size <= 4096 or at + name_size + description_size > len(original):
            raise ValueError('Invalid bounded inventory labels')
        at += name_size + description_size
        if identity not in source or identity in seen or atlas >= textures:
            raise ValueError('Unknown/duplicate inventory item')
        item = source[identity]
        expected = struct.pack('<4I5f', identity, item['flags'], atlas, item['shape_index'], *item['uv_rect'], item['aspect'])
        if prefix != expected or texture_ids[atlas] != item['texture_resource_id']:
            raise ValueError('Native inventory geometry or flags differ')
        name, description = (inventory_label(item[key].encode('ascii')) for key in ('name_raw','description_raw'))
        if not 0 < len(name) <= 256 or not 0 < len(description) <= 4096:
            raise ValueError('Invalid original English inventory labels')
        result += prefix + struct.pack('<2I', len(name), len(description)) + name + description
        seen.add(identity)
    if at != len(original):
        raise ValueError('Trailing inventory bytes')
    return bytes(result)


def export(source, assets, output):
    source, assets = imp.checked_path(source), imp.checked_path(assets)
    output = imp.output_guard(output, (source, assets))
    source_manifest=source.parent/'files-manifest.json';imp.no_reparse(source_manifest)
    if sha(source_manifest.read_bytes())!=imp.SOURCE_SHA:
        raise ValueError('Unknown original Dreamcast extraction')
    for name in ('LEVELA.JDC','AZ01.DC','BZ01.DC'):
        imp.no_reparse(source/name)
    for relative in ('inventory.bin','levelB/inventory.bin'):
        imp.no_reparse(assets/relative)
    resources = Resources(source)
    native = read_inventory(resources, 'A', union=True)
    output.mkdir(parents=True)
    (output / 'INCOMPLETE').write_text('Language sidecars incomplete\n', encoding='ascii')
    manifest = {'format': 'EDDREAMCAST-LANGUAGES1', 'default': 'ru', 'languages': ['en', 'ru'],
                'source': str(source), 'source_manifest_sha256':imp.SOURCE_SHA,'base': str(assets), 'inventory_items': len(native['items']),
                'policy': 'Optional English sidecars. Russian/base files stay byte-exact; native FONT5-style161 serves both languages.',
                'files': [], 'resources': list(native['resources'].values()),
                'script': native['script'], 'script_sha256': native['script_sha256']}
    for level, scope in (('A', 'AZ01.DC'), ('B', 'BZ01.DC')):
        base = assets if level == 'A' else assets / 'levelB'
        prefix = Path() if level == 'A' else Path('levelB')
        for identity in (0x0b942034, 0x0b942054):
            raw, record = resources.get((scope,), identity, 1)
            image, _ = decode_texture(raw, identity)
            payload = encode_texture(identity, image)
            relative = prefix / 'gui/languages/en' / f'{identity:08x}.rgba'
            target = output / relative;target.parent.mkdir(parents=True, exist_ok=True);target.write_bytes(payload)
            manifest['files'].append({'path': relative.as_posix(), 'bytes': len(payload), 'sha256': sha(payload), 'source': record})
        original = (base / 'inventory.bin').read_bytes()
        payload = english_inventory(original, native)
        relative = prefix / 'gui/languages/en/inventory.bin'
        (output / relative).write_bytes(payload)
        manifest['files'].append({'path': relative.as_posix(), 'bytes': len(payload), 'sha256': sha(payload),
                                  'base_inventory_sha256': sha(original), 'labels': 'original DC ObjDB lookup IDs;121-item explicit debug union'})
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    (output / 'INCOMPLETE').unlink()
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'assets', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    result = export(args.source, args.assets, args.output)
    print(f"English native UI sidecars: {len(result['files'])} files, {result['inventory_items']} inventory labels")
