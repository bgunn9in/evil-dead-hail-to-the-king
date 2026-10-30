"""Export original Dreamcast A/B authored GUI and explicit 121-item inventory overlays."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct

from ed_assets import parse_dc_archive
from ed_dc_model import decode_mesh, decode_texture
from ed_dc_audio import checked_output
from ed_gui_assets import pack_layout, pack_texts
from ed_inventory import QUERIES
from ed_objects import encode_mesh, encode_texture
from ed_scripts import ScriptPackage
from ed_vm import Interpreter
from export_menu import pack_resources


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


class Resources:
    """Exact ID+flags lookup; priority is explicit and differing aliases are rejected."""
    def __init__(self, source):
        self.source = Path(source)
        self.archives = {}

    def archive(self, name):
        if name not in self.archives:
            raw = (self.source / name).read_bytes()
            table = {}
            for entry in parse_dc_archive(raw):
                key = entry.resource_id, entry.flags
                table.setdefault(key, []).append(entry)
            self.archives[name] = raw, table, digest(raw)
        return self.archives[name]

    def get(self, names, identity, flags):
        found = []
        for name in names:
            raw, table, sha = self.archive(name)
            matches = table.get((identity, flags), [])
            if len(matches) > 1:
                raise ValueError(f'Ambiguous original resource {name}/{identity:08x}/{flags}')
            if matches:
                entry = matches[0]
                data = raw[entry.offset:entry.offset + entry.size]
                found.append((data, {'archive': name, 'archive_sha256': sha, 'entry': entry.index,
                                    'id': f'{identity:08x}', 'flags': flags, 'offset': entry.offset,
                                    'bytes': len(data), 'sha256': digest(data)}))
        if not found:
            raise ValueError(f'Missing original resource {identity:08x}/{flags}')
        if any(raw != found[0][0] for raw, _ in found[1:]):
            raise ValueError(f'Conflicting original resource {identity:08x}/{flags}')
        return found[0][0], {**found[0][1], 'identical_alternatives': [r for _, r in found[1:]]}


def text_payload(raw):
    end = raw.find(b'\0')
    if end < 0 or any(raw[end:]) or end + 1 > 32767:
        raise ValueError('Invalid original Dreamcast text allocation')
    if any(c > 126 or c < 32 and c not in (9, 10, 13) for c in raw[:end]):
        raise ValueError('Unsupported original Dreamcast text glyph')
    return raw[:end + 1]


def pack_uv(models):
    """Preserve original vertex ordinals; EDOBJ1 expands triangle vertices."""
    result = bytearray(struct.pack('<8sI', b'EGUIUV1\0', len(models)))
    if not 0 < len(models) <= 1000:
        raise ValueError('Invalid GUI UV model count')
    for identity, parts in sorted(models.items()):
        if identity >> 24 != 11 or not 0 < len(parts) <= 256:
            raise ValueError('Invalid GUI UV model')
        result += struct.pack('<2I', identity, len(parts))
        for part in parts:
            uv = part['uv']
            indices = [i for triangle in part['triangles'] for i in triangle]
            if (not 0 < len(uv) <= 65536 or len(indices) > 1000000 or len(indices) % 3 or
                    any(len(p) != 2 or any(not math.isfinite(v) for v in p) for p in uv) or
                    any(not 0 <= i < len(uv) for i in indices)):
                raise ValueError('Invalid original GUI UV vertex/index range')
            result += struct.pack('<2I', len(uv), len(indices))
            result += b''.join(struct.pack('<2f', *p) for p in uv)
            result += struct.pack(f'<{len(indices)}H', *indices)
    return bytes(result)


def read_gui(resources, level):
    scopes = ('AZ01.DC', 'AA01.DC', 'AK01.DC') if level == 'A' else ('BZ01.DC', 'BB01.DC', 'BM01.DC')
    models, textures, originals, provenance = {}, {}, {}, []
    keys = sorted({key for name in scopes for key in resources.archive(name)[1] if key[0] >> 24 == 11})
    for identity, flags in keys:
        raw, record = resources.get(scopes, identity, flags)
        provenance.append(record)
        originals[f'{identity:08x}-{flags}.resource'] = raw
        if flags == 0:
            models[identity] = decode_mesh(raw)
        elif flags == 1:
            image, info = decode_texture(raw, identity)
            textures[identity] = encode_texture(identity, image), info
        else:
            raise ValueError('Unsupported original GUI resource flags')
    if not {0x0b142038, 0x0b142088}.issubset(models) or 0x0b942034 not in textures:
        raise ValueError('Missing original main-menu/GameOver resources')
    texts, text_sources = {}, []
    for path in sorted(resources.source.glob(level + '*.DC')):
        raw, table, sha = resources.archive(path.name)
        scope = 0 if path.name == scopes[0] else struct.unpack_from('<I', raw)[0]
        for (identity, flags), matches in table.items():
            if (identity >> 24) & 31 != 17:
                continue
            if flags:
                raise ValueError('Invalid original text flags')
            if len(matches) != 1:
                raise ValueError('Ambiguous original GUI text')
            entry = matches[0]
            payload = raw[entry.offset:entry.offset + entry.size]
            value = text_payload(payload)
            key = scope, identity
            if key in texts and texts[key] != value:
                raise ValueError('Conflicting scoped original GUI text')
            texts[key] = value
            text_sources.append({'archive': path.name, 'archive_sha256': sha, 'entry': entry.index,
                                 'scope': scope, 'id': f'{identity:08x}', 'bytes': len(payload),
                                 'logical_bytes': len(value), 'sha256': digest(payload)})
    menu = []
    for identity, flags in sorted({key for name in scopes[:2] for key in resources.archive(name)[1]
                                   if key[0] >> 24 == 11 or (key[0] >> 24) & 31 == 17}):
        raw, _ = resources.get(scopes[:2], identity, flags)
        menu.append({'id': identity, 'flags': flags, 'payload': raw})
    return {'models': models, 'textures': textures, 'texts': texts, 'originals': originals, 'menu': menu,
            'resources': provenance, 'text_sources': text_sources}


def read_inventory(resources, level, union=False):
    script_name = f'LEVEL{level}.JDC'
    script = (resources.source / script_name).read_bytes()
    package = ScriptPackage(script)
    vm = Interpreter(package)
    methods = {key: vm.method_index('ObjDB', name, '(I)I') for key, name in QUERIES.items()}
    names = ('AZ01.DC', 'BZ01.DC') if union else (('AZ01.DC',) if level == 'A' else ('BZ01.DC',))
    available = {key for name in names for key in resources.archive(name)[1]}
    candidates = sorted({key for row in package.disassemble(methods['flags']) for key, _ in row.get('cases', [])})
    items, textures, meshes, sources, originals, unavailable = [], {}, {}, {}, {}, []
    def get(identity, flags):
        raw, record = resources.get(names, identity, flags)
        sources[f'{identity:08x}/{flags}'] = record
        originals[f'{identity:08x}-{flags}.resource'] = raw
        return raw
    for identity in candidates:
        item = {key: vm.invoke(method, args=(identity,)) & 0xffffffff for key, method in methods.items()}
        if not all(item[key] for key in ('flags', 'name_resource_id', 'description_resource_id')):
            continue
        required = [(item[key], int(key == 'texture_resource_id')) for key in
                    ('name_resource_id', 'description_resource_id', 'geometry_resource_id', 'texture_resource_id')]
        if any(key not in available for key in required):
            unavailable.append(identity)
            continue
        item['item_id'] = identity
        for key in ('name', 'description'):
            item[key + '_raw'] = text_payload(get(item[key + '_resource_id'], 0))[:-1].decode('ascii')
        mesh, texture = item['geometry_resource_id'], item['texture_resource_id']
        if mesh not in meshes:
            meshes[mesh] = decode_mesh(get(mesh, 0))
        if texture not in textures:
            textures[texture] = decode_texture(get(texture, 1), texture)
        if item['shape_index'] >= len(meshes[mesh]):
            raise ValueError('Original inventory shape is outside mesh')
        part = meshes[mesh][item['shape_index']]
        if any(b != 65535 for b in part['bone_indices']):
            raise ValueError('Original GUI icon is unexpectedly skinned')
        uv = [min(p[0] for p in part['uv']), min(p[1] for p in part['uv']),
              max(p[0] for p in part['uv']), max(p[1] for p in part['uv'])]
        width = max(p[0] for p in part['positions']) - min(p[0] for p in part['positions'])
        height = max(p[1] for p in part['positions']) - min(p[1] for p in part['positions'])
        if not 0 <= uv[0] < uv[2] <= 1 or not 0 <= uv[1] < uv[3] <= 1 or width <= 0 or height <= 0 or not 0 < width / height < 20:
            raise ValueError('Invalid original inventory UV/extent')
        item.update(uv_rect=uv, aspect=width / height)
        items.append(item)
    if not items or len(textures) > 8:
        raise ValueError('Invalid original inventory counts')
    return {'items': items, 'textures': textures, 'resources': sources, 'originals': originals,
            'unavailable_ids': unavailable, 'script': script_name, 'script_sha256': digest(script),
            'lookup_methods': methods, 'policy': 'AZ01 first/BZ01 fallback; identical aliases only; LEVELA.JDC lookups' if union else 'Own-package shared archive only'}


def pack_inventory(data):
    ids = list(data['textures'])
    result = bytearray(struct.pack('<8s2I', b'EDINV1\0\0', len(ids), len(data['items'])))
    for identity in ids:
        image = data['textures'][identity][0]
        if not 0 < image.width <= 2048 or not 0 < image.height <= 2048:
            raise ValueError('Invalid inventory atlas dimensions')
        result += struct.pack('<3I', identity, image.width, image.height)
    for item in data['items']:
        name, description = item['name_raw'].encode('ascii'), item['description_raw'].encode('ascii')
        if not 0 < len(name) <= 256 or not 0 < len(description) <= 4096:
            raise ValueError('Invalid inventory label length')
        result += struct.pack('<4I5f2I', item['item_id'], item['flags'], ids.index(item['texture_resource_id']),
                              item['shape_index'], *item['uv_rect'], item['aspect'], len(name), len(description))
        result += name + description
    return bytes(result)


def write_inventory(output, data):
    output.mkdir(parents=True, exist_ok=True)
    (output / 'inventory.bin').write_bytes(pack_inventory(data))
    for identity, (image, _) in data['textures'].items():
        (output / f'inventory_{identity:08x}.rgba').write_bytes(image.tobytes())
    metadata = {key: value for key, value in data.items() if key not in ('textures', 'originals')}
    metadata.update(item_count=len(data['items']), atlas_count=len(data['textures']))
    (output / 'inventory-source.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')


def debug_union_overlay(gui, inventory):
    """Keep native GUI separate from the resident resources required by debug items."""
    models, textures = dict(gui['models']), dict(gui['textures'])
    files, sources = {}, []
    for item in inventory['items']:
        mesh, texture = item['geometry_resource_id'], item['texture_resource_id']
        if mesh not in models:
            models[mesh] = decode_mesh(inventory['originals'][f'{mesh:08x}-0.resource'])
            files[f'gui/all-models/{mesh:08x}.mesh'] = encode_mesh(mesh, models[mesh])[0]
            sources.append(inventory['resources'][f'{mesh:08x}/0'])
        if texture not in textures:
            image, info = inventory['textures'][texture]
            textures[texture] = encode_texture(texture, image), info
            files[f'gui/textures/{texture:08x}.rgba'] = textures[texture][0]
            sources.append(inventory['resources'][f'{texture:08x}/1'])
        if item['shape_index'] >= len(models[mesh]):
            raise ValueError('Debug inventory shape is outside its resident original model')
    if sources:
        files['gui/layout.bin'] = pack_layout(models)
        files['gui/uv-ordinals.bin'] = pack_uv(models)
    return files, {'format': 'EDDREAMCASTUI-DEBUG-UNION1', 'models': len(models), 'parts': sum(map(len, models.values())),
                   'textures': len(textures), 'inventory_items_covered': len(inventory['items']), 'added_resources': sources,
                   'policy': 'Explicit resident autoload bridge for debug inventory union. Apply after own-package GUI; usage remains original script requested IDs. Not native LevelA cross-package autoload.'}


def export_ui(source, output, catalog=None, font=None):
    source = Path(source).resolve(strict=True)
    output = checked_output(source, output)
    if output.exists() or output == source or source in output.parents or output in source.parents:
        raise ValueError('Use a new output directory outside original files')
    if (catalog is None) != (font is None):
        raise ValueError('Russian catalog and declared font directory must be supplied together')
    resources = Resources(source)
    gui = {level: read_gui(resources, level) for level in ('A', 'B')}
    native = {level: read_inventory(resources, level) for level in ('A', 'B')}
    union = read_inventory(resources, 'A', union=True)
    output.mkdir(parents=True)
    (output / 'INCOMPLETE').write_text('Dreamcast UI export incomplete\n', encoding='ascii')
    metadata = {'format': 'EDDREAMCASTUI1', 'source': str(source), 'profiles': {},
                'inventory_policy': union['policy'], 'limits': ['GUI sets remain own-package; debug inventory union is explicit, not native cross-package autoload.', 'Baked Latin atlas labels are untouched original artwork; their localization is a separate checked edit.', 'Opaque PVRT tails are retained verbatim in original-resources.']}
    for level in ('A', 'B'):
        target = output / level
        data = gui[level]
        for sub in ('gui/all-models', 'gui/models', 'gui/textures', 'original-resources'):
            (target / sub).mkdir(parents=True, exist_ok=True)
        (target / 'gui/layout.bin').write_bytes(pack_layout(data['models']))
        (target / 'gui/text.bin').write_bytes(pack_texts(data['texts']))
        (target / 'gui/uv-ordinals.bin').write_bytes(pack_uv(data['models']))
        for identity, parts in data['models'].items():
            (target / f'gui/all-models/{identity:08x}.mesh').write_bytes(encode_mesh(identity, parts)[0])
        (target / 'gui/models/game-over.mesh').write_bytes(encode_mesh(0x0b142088, data['models'][0x0b142088])[0])
        for identity, (encoded, _) in data['textures'].items():
            (target / f'gui/textures/{identity:08x}.rgba').write_bytes(encoded)
        title = target / ('aa01' if level == 'A' else 'bb01')
        title.mkdir()
        (title / 'main-menu.mesh').write_bytes(encode_mesh(0x0b142038, data['models'][0x0b142038])[0])
        (title / 'main-menu.texture').write_bytes(data['textures'][0x0b942034][0])
        (title / 'menu-resources.bin').write_bytes(pack_resources(data['menu']))
        for name, raw in {**data['originals'], **union['originals']}.items():
            (target / 'original-resources' / name).write_bytes(raw)
        write_inventory(target, union)
        write_inventory(output / 'native-inventory' / level, native[level])
        extra, bridge = debug_union_overlay(data, union)
        if extra:
            overlay = output / 'debug-inventory-union' / level
            for name, raw in extra.items():
                path = overlay / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(raw)
            (overlay / 'source.json').write_text(json.dumps(bridge, indent=2) + '\n', encoding='utf-8')
        if catalog is not None:
            from compile_localization import font_charset, inventory_text_ids, pack_inventory as localize_inventory, pack_text, translations
            strings = translations(json.loads(Path(catalog).read_text('utf-8')), level, font_charset(Path(font) / 'font.txt'))
            (target / 'gui/text-ru.bin').write_bytes(pack_text(strings, (target / 'gui/text.bin').read_bytes()))
            package = ScriptPackage((source / f'LEVEL{level}.JDC').read_bytes())
            (target / 'inventory.bin').write_bytes(localize_inventory(pack_inventory(union), strings, lambda ids: inventory_text_ids(package, ids)))
            for name in ('font.txt', 'font.bmp'):
                shutil.copyfile(Path(font) / name, target / name)
        record = {'models': len(data['models']), 'parts': sum(map(len, data['models'].values())), 'textures': len(data['textures']),
                  'text_count': len(data['texts']), 'inventory_items': len(union['items']), 'native_inventory_items': len(native[level]['items']),
                  'resources': data['resources'], 'text_sources': data['text_sources'],
                  'texture_metadata': {f'{k:08x}': info for k, (_, info) in data['textures'].items()}}
        (target / 'gui/source.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        metadata['profiles'][level] = {k: v for k, v in record.items() if k not in ('resources', 'text_sources', 'texture_metadata')}
        metadata['profiles'][level]['debug_union'] = bridge
    if catalog is not None:
        metadata['localization'] = {'catalog_sha256': digest(Path(catalog).read_bytes()),
                                    'font_metadata_sha256': digest((Path(font) / 'font.txt').read_bytes()),
                                    'font_bitmap_sha256': digest((Path(font) / 'font.bmp').read_bytes())}
    metadata['files'] = [{'path': p.relative_to(output).as_posix(), 'bytes': p.stat().st_size, 'sha256': digest(p.read_bytes())}
                         for p in sorted(output.rglob('*')) if p.is_file() and p.name != 'INCOMPLETE']
    (output / 'source.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    (output / 'INCOMPLETE').unlink()
    return metadata


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--catalog', type=Path)
    parser.add_argument('--font', type=Path)
    args = parser.parse_args()
    print(json.dumps(export_ui(args.source, args.output, args.catalog, args.font)['profiles'], indent=2))
