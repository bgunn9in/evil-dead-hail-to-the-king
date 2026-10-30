"""Build renderer/geometry profiles entirely from original Dreamcast bytes."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import struct
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from PIL import Image
from ed_assets import parse_dc_archive
from ed_camera import parse_scene, view_projection
from ed_collision import parse_collision
from ed_dc_audio import checked_output
from ed_dc_masks import decode_dc_masks, metadata
from ed_dc_model import decode_mesh, decode_texture
from ed_model import decode_skeleton, decode_animations, sample_pose
from ed_portals import parse_portals
from ed_scripts import ScriptPackage
from ed_triggers import parse_triggers
from audit_scene_coverage import registry, literal_arguments
from export_dreamcast_masks import write_layers
from export_episode import arrival_spawn, portal_manifest, pack_portals, spawn_clearance
from export_scene import pack_camera_frames, pack_collision, pack_floor, pack_triggers, pack_doors, pack_pose, ground_axis

def sha(data):
    return hashlib.sha256(data).hexdigest()

def resource(data, identity, flags):
    rows = [e for e in parse_dc_archive(data) if e.resource_id == identity and e.flags == flags]
    if len(rows) != 1:
        raise ValueError(f'Expected one original DC resource{identity:08x}/{flags}')
    e = rows[0]
    return data[e.offset:e.offset+e.size]

def checked_frame(directory, record, index):
    if record['avi_index'] != index or record['native_bmp'] != f'frame_{index:06d}.bmp':
        raise ValueError('Native frame manifest order/name changed')
    path = directory / 'native-rgb' / record['native_bmp']
    data = path.read_bytes()
    if sha(data) != record['bmp_sha256']:
        raise ValueError('Native RGB source frame hash changed')
    with Image.open(path) as image:
        if image.format != 'BMP' or image.size != (320, 240) or image.mode != 'RGB' or sha(image.tobytes()) != record['rgb_sha256']:
            raise ValueError('Expected unchanged RGB320x240 native BMP')
    return path

def scene_text(scene, spawn, vertices):
    lines = ['EDSCENE2', f'{vertices} {len(scene["tracks"])}', ' '.join(map(str, spawn)), '1 1.3', '0 0 30']
    for track in scene['tracks']:
        camera = view_projection(track['frames'][0])
        right, forward = ground_axis(camera['right3']), ground_axis(camera['forward3'])
        lines.extend([' '.join(map(str, camera['viewProjection16'])), ' '.join(map(str, right+forward))])
    return '\n'.join(lines)+'\n'

def load_contexts(source):
    packages, contexts, switches, factories = {}, {}, [], {}
    for level in 'AB':
        raw = (source / f'LEVEL{level}.JDC').read_bytes()
        package = ScriptPackage(raw);packages[level] = package
        factories[level] = registry(package, 'Level'+level)
        for method in package.methods:
            for index, row in enumerate(code := package.disassemble(method['index'])):
                target = row.get('method', {})
                if (target.get('class_name'), target.get('name'), target.get('descriptor')) == ('ed/base/External', 'sceneSwitch', '(IIII)V'):
                    args = literal_arguments(code, index, 4)
                    if args is not None:
                        switches.append({'method': method['index'], 'pc': row['offset'], 'owner': method['class_name'],
                                         'destination': args[0], 'camera': args[1], 'marker': args[2], 'degrees': args[3], 'package': level})
        for row in factories[level]:
            name = row['scene']
            path = source / (name.upper()+'.DC')
            data = path.read_bytes()
            identity = struct.unpack_from('<I', data)[0]
            if identity != row['scene_id']:
                raise ValueError('Native factory/archive sceneID mismatch')
            entries = [e for e in parse_dc_archive(data) if e.resource_id >> 24 == 15 and not e.flags]
            if len(entries) != 1:
                raise ValueError('Expected one original scene payload')
            e = entries[0];payload = data[e.offset:e.offset+e.size]
            contexts[name] = {'name': name, 'id': identity, 'payload': payload, 'scene': parse_scene(payload),
                              'collision': parse_collision(payload), 'portals': parse_portals(payload, identity),
                              'triggers': parse_triggers(payload, camera_events=True), 'archive': path.name,
                              'archive_sha256': sha(data), 'entry': e.index, 'level': level}
    return packages, contexts, switches, factories

def loader_ash(source, output):
    archive = (source / 'AZ01.DC').read_bytes()
    parts = decode_mesh(resource(archive, 0x00024000, 0))
    bones = decode_skeleton(resource(archive, 0x00824000, 0))
    clip = next(c for c in decode_animations(resource(archive, 0x01024000, 0)) if c['resource_id'] == 0x01024084)
    mesh = pack_pose(sample_pose(parts, bones, clip, 0))
    texture, _ = decode_texture(resource(archive, 0x08024000, 1), 0x08024000)
    (output / 'ash.bin').write_bytes(mesh);texture.save(output / 'ash.bmp')
    return {'source_archive': 'AZ01.DC', 'source_archive_sha256': sha(archive), 'vertex_count': len(mesh)//32,
            'clip': '01024084', 'frame': 0, 'parts': [0, 1, 10], 'mesh_sha256': sha(mesh),
            'role': 'Explicit loader-only static original Ash. Actual JDC/GameplayActors owns live pose, action and spawn.',
            'scene_header_scale_speed': [1, 1.3], 'scene_header_animation_frames': [0, 0]}

def export(source, frames, output):
    source, frames = Path(source).resolve(), Path(frames).resolve()
    output = checked_output(source, output)
    if output.exists():
        raise FileExistsError(output)
    if output == frames or frames in output.parents or output in frames.parents:
        raise ValueError('Scene profiles must be outside the native frame cache')
    native = json.loads((frames / 'source.json').read_text())
    if native['format'] != 'EDDCTM2XNATIVE1' or native['conversion_status'] != 'PASS' or native['native_size'] != [320, 240]:
        raise ValueError('Complete checked native RGB cache is required')
    originals = {f['path']: f['sha256'] for f in json.loads((source.parent / 'files-manifest.json').read_text())['files']}
    packages, contexts, switches, factories = load_contexts(source)
    for context in contexts.values():
        if context['archive_sha256'] != originals[context['archive']]:
            raise ValueError('Native scene differs from original extraction manifest')
    cached = {Path(a['file']).stem.lower(): a for a in native['archives']}
    selected = {name: c for name, c in contexts.items() if name in cached}
    destinations = {c['id']: c['portals'] for c in selected.values()}
    arrival_sources = {c['id']: c['portals'] for c in contexts.values()}
    output.mkdir(parents=True)
    (output / 'INCOMPLETE').write_text('Native scene/profile export has not completed.\n')
    static = output / 'loader-static';static.mkdir()
    actor = loader_ash(source, static)
    (static / 'source.json').write_text(json.dumps(actor, indent=2)+'\n')
    result = {'format': 'EDDCNATIVESCENES1', 'status': 'RUNNING', 'source_manifest_sha256': sha((source.parent/'files-manifest.json').read_bytes()),
              'frames_manifest_sha256': sha((frames/'source.json').read_bytes()), 'camera_projection': 'Existing verified port perspective; original SH4 lens calibration remains separate.',
              'source_factories': factories, 'loader_actor': actor, 'scenes': [],
              'unsupported': [{'scene': name, 'reason': 'Registered native scene has no original AVI; no asset aliases guessed.'} for name in contexts if name not in selected],
              'runtime_scope': 'Renderer/native geometry adapter only. Root stages actual JDC, actors, audio, GUI and world metadata separately.'}
    far_depth = struct.pack('<f', 1)*(320*240)
    for name, context in selected.items():
        destination = output / name;destination.mkdir()
        record = cached[name]
        cache = frames / name.upper()
        cached_manifest = (cache/'source.json').read_bytes()
        if sha(cached_manifest) != record['manifest_sha256']:
            raise ValueError('Native per-AVI manifest changed')
        images = json.loads(cached_manifest)
        if images['source_sha256'] != originals[name.upper()+'.AVI']:
            raise ValueError('Native frame cache belongs to another original AVI')
        dcv_data = (source / (name.upper()+'V.DC')).read_bytes()
        if sha(dcv_data) != originals[name.upper()+'V.DC']:
            raise ValueError('Original DCV hash changed')
        masks = [e for e in parse_dc_archive(dcv_data) if e.resource_id >> 24 == 8 and not e.flags]
        scene, collision = context['scene'], context['collision']
        (destination/'scene.native').write_bytes(context['payload'])
        (destination/'camera-frames.bin').write_bytes(pack_camera_frames(scene['tracks']))
        (destination/'background_size.txt').write_text('EDPROFILEBG1\n320 240 0.875\n', encoding='ascii')
        collision_bytes, flat_height = pack_collision(collision)
        (destination/'collision.bin').write_bytes(collision_bytes)
        (destination/'floor.bin').write_bytes(pack_floor(collision['floor']))
        (destination/'triggers.bin').write_bytes(pack_triggers(context['triggers']))
        portals = portal_manifest(context['portals'], destinations)
        (destination/'portals.bin').write_bytes(pack_portals(portals))
        (destination/'portals.json').write_text(json.dumps(portals, indent=2)+'\n')
        cameras, used = [], set()
        for track in scene['tracks']:
            for frame in track['frames']:
                index = frame['background_index']
                if index >= len(images['frames']) or index >= len(masks):
                    raise ValueError('Native camera frame outside original image/mask allocation')
                image = checked_frame(cache, images['frames'][index], index)
                suffix = str(track['index'])+(f'_{frame["index"]}' if frame['index'] else '')
                shutil.copyfile(image, destination/f'background_{suffix}.bmp')
                (destination/f'background_{suffix}.depth').write_bytes(far_depth)
                e = masks[index];payload = dcv_data[e.offset:e.offset+e.size];layers = decode_dc_masks(payload)
                write_layers(layers, destination, suffix)
                used.add(index)
                cameras.append(metadata({'track': track['index'], 'frame': frame['index'], 'avi_index': index,
                    'image_rgb_sha256': images['frames'][index]['rgb_sha256'], 'mask_entry': e.index,
                    'mask_payload_sha256': sha(payload), 'raw_camera': frame, 'layers': layers}))
        menu = name in ('aa01', 'bb01')
        if menu:
            (destination/'menu.txt').write_text(f'EDMENU1\n{name}\n{len(scene["tracks"])}\n', encoding='ascii')
            spawn, role = None, 'Original nonwalking menu; no spawn marker invented.'
        else:
            spawn, role = arrival_spawn(name, contexts, arrival_sources, switches)
            if spawn_clearance(collision, spawn[0], spawn[2]) <= 0:
                raise ValueError('Unchanged authored loader marker intersects source solid')
            (destination/'scene.txt').write_text(scene_text(scene, spawn, actor['vertex_count']), encoding='ascii')
            shutil.copyfile(static/'ash.bin', destination/'ash.bin');shutil.copyfile(static/'ash.bmp', destination/'ash.bmp')
            empty = {'scene_name': name, 'doors': [], 'unsupported_portals': [], 'policy': 'Actual JDC owns all passage enable/use state; portals.bin retains native geometry.'}
            (destination/'doors.bin').write_bytes(pack_doors(empty))
        manifest = {'format': 'EDDCNATIVESCENE1', 'scene': name, 'scene_id': context['id'], 'level': context['level'],
                    'source_archive': context['archive'], 'source_archive_sha256': context['archive_sha256'],
                    'scene_entry': context['entry'], 'scene_sha256': sha(context['payload']), 'dcv_sha256': sha(dcv_data),
                    'native_image_size': [320, 240], 'viewport': [640, 480], 'rgb_factor': .875,
                    'camera_frame_counts': [len(t['frames']) for t in scene['tracks']], 'cameras': cameras,
                    'spawn': spawn, 'spawn_role': role, 'loader_only_actor': None if menu else actor,
                    'floor_triangles': len(collision['floor']['triangles']), 'static_solids': len(collision['solids']),
                    'collision_flat_height': flat_height, 'camera_triggers': len(context['triggers']['triggers']),
                    'portals': len(portals['portals']), 'unreferenced_avi_frames': sorted(set(range(len(images['frames'])))-used),
                    'limits': 'Live state comes from actual JDC. Native PVR raster/lens/heading equivalence is not established by this asset export.'}
        manifest['files'] = [{'path': p.name, 'bytes': p.stat().st_size, 'sha256': sha(p.read_bytes())} for p in sorted(destination.iterdir()) if p.is_file()]
        (destination/'source.json').write_text(json.dumps(manifest, indent=2)+'\n')
        result['scenes'].append({'scene': name, 'level': context['level'], 'menu': menu, 'cameras': len(scene['tracks']),
                                 'frames': len(cameras), 'layers': sum(len(c['layers']) for c in cameras),
                                 'manifest_sha256': sha((destination/'source.json').read_bytes())})
        print(name, len(cameras), 'native frames;', len(scene['tracks']), 'tracks;', 'menu' if menu else 'authored loader marker', flush=True)
    result['status'] = 'PASS'
    (output/'source.json').write_text(json.dumps(result, indent=2)+'\n')
    (output/'INCOMPLETE').unlink()
    print('Native scenes complete:', len(result['scenes']), 'scenes;', sum(s['frames'] for s in result['scenes']), 'frames', flush=True)
    return result

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--frames', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    export(args.source, args.frames, args.output)
