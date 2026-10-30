"""Export an isolated AA01/AK01 visual inspection profile from original DC bytes.

Input frames must be produced by the separately licensed NihAV TM2X tool. This
does not replace the game's active assets or execute either original binary.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

from PIL import Image

from ed_assets import parse_dc_archive
from ed_camera import parse_scene, view_projection
from ed_dc_model import decode_mesh, decode_texture
from ed_model import decode_animations, decode_skeleton, sample_pose
from export_scene import BODY_PARTS, pack_pose


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_pgmyuv(data):
    # NihAV writes three full-resolution YUVJ planes into a tall PGM carrier.
    header = b'P5\n320 720\n255\n'
    if not data.startswith(header) or len(data) != len(header) + 320 * 240 * 3:
        raise ValueError('Expected complete NihAV TM2X320x240 YUVJ444 frame')
    return data[len(header):]


def convert_frame(data, ffmpeg):
    result = subprocess.run([str(ffmpeg), '-v', 'error', '-f', 'rawvideo', '-pixel_format', 'yuvj444p',
                             '-video_size', '320x240', '-i', 'pipe:0', '-frames:v', '1',
                             '-sws_flags', 'accurate_rnd+bitexact', '-f', 'rawvideo', '-pix_fmt', 'rgba', 'pipe:1'],
                            input=read_pgmyuv(data), capture_output=True, timeout=20,
                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    if result.returncode or len(result.stdout) != 320 * 240 * 4:
        raise ValueError('FFmpeg TM2X frame color conversion failed: ' + result.stderr[:1024].decode(errors='replace'))
    return Image.frombytes('RGBA', (320, 240), result.stdout)


def _resource(source, name, identity, flags=0):
    path = source / name
    data = path.read_bytes()
    entries = [entry for entry in parse_dc_archive(data) if entry.resource_id == identity and entry.flags == flags]
    if len(entries) != 1:
        raise ValueError(f'Expected one exact DC resource {name}/{identity:08x}/{flags}')
    entry = entries[0]
    raw = data[entry.offset:entry.offset + entry.size]
    return raw, {'archive': name, 'archive_sha256': digest(data), 'entry': entry.index,
                 'id': f'{identity:08x}', 'flags': flags, 'offset': entry.offset,
                 'bytes': len(raw), 'sha256': digest(raw)}


def export_profile(source, decoded, output, scene_name, ffmpeg):
    source, decoded, output = (Path(p).resolve() for p in (source, decoded, output))
    scene_name = scene_name.upper()
    if scene_name not in ('AA01', 'AK01'):
        raise ValueError('Only the verified first AA01/AK01 visual profiles are supported')
    if output == source or source in output.parents or output == decoded or decoded in output.parents:
        raise ValueError('Generated profile must be outside the original/decoded input directories')
    if output.exists():
        raise FileExistsError('A visual profile requires a new output directory')
    archive = (source / (scene_name + '.DC')).read_bytes()
    entries = [e for e in parse_dc_archive(archive) if e.resource_id >> 24 == 15 and e.flags == 0]
    if len(entries) != 1:
        raise ValueError('Expected one original DC scene payload')
    entry = entries[0]
    native = archive[entry.offset:entry.offset + entry.size]
    scene = parse_scene(native)
    v = (source / (scene_name + 'V.DC')).read_bytes()
    backgrounds = [e for e in parse_dc_archive(v) if e.resource_id >> 24 == 8 and e.flags == 0]
    frames = sorted(decoded.glob('frame00_*.pgm'))
    expected = 4 if scene_name == 'AA01' else 174
    if len(backgrounds) != expected or len(frames) != expected or any(
            path.name != f'frame00_{i + 1:06d}.pgm' for i, path in enumerate(frames)):
        raise ValueError('Decoded frame/DCV count or frame sequence differs from the checked AVI mapping')
    # Native masked-camera depth conversion remains separate from this first proof.
    tracks = scene['tracks'] if scene_name == 'AA01' else scene['tracks'][:1]
    for track in tracks:
        for frame in track['frames']:
            index = frame['background_index']
            if index >= len(backgrounds):
                raise ValueError('Source camera index exceeds original DCV/AVI frames')
            e = backgrounds[index]
            if v[e.offset:e.offset + e.size] != bytes(4):
                raise ValueError('This inspection profile requires a source camera with no foreground masks')
    output.mkdir(parents=True)
    (output / 'INCOMPLETE').write_text('Visual export has not completed.\n', encoding='ascii')
    originals = output / 'source-resources'; originals.mkdir()
    (originals / 'scene.native').write_bytes(native)
    info = {'format': 'EDDCSVISUAL1', 'platform': 'Dreamcast', 'scene': scene_name.lower(),
            'scope': 'Visual inspection only; no JDC execution or full-game asset replacement.',
            'scene_resource': {'archive': scene_name + '.DC', 'entry': entry.index,
                               'id': f'{entry.resource_id:08x}', 'bytes': len(native), 'sha256': digest(native)},
            'source_archive_sha256': digest(archive), 'source_avi_sha256': digest((source / (scene_name + '.AVI')).read_bytes()),
            'source_dcv_sha256': digest(v), 'native_background_size': [320, 240],
            'compatibility_background_size': [640, 384],
            'background_adapter': 'PREVIEW ONLY: nearest resampling to the existing renderer texture dimensions; full frame, no crop. Native320x240 PNG retained. This does not prove native Dreamcast pixel aspect.',
            'display_aspect': [4, 3], 'cameras': [], 'resources': []}
    for track in tracks:
        for frame in track['frames']:
            index, camera = frame['background_index'], view_projection(frame)
            pgm = frames[index].read_bytes()
            image = convert_frame(pgm, ffmpeg)
            suffix = str(track['index']) if not frame['index'] else f"{track['index']}_{frame['index']}"
            image.save(output / f'background_{suffix}.native.png')
            image.convert('RGB').resize((640, 384), Image.Resampling.NEAREST).save(output / f'background_{suffix}.bmp')
            (output / f'background_{suffix}.depth').write_bytes(struct.pack('<f', 1.0) * (640 * 384))
            info['cameras'].append({'track': track['index'], 'frame': frame['index'], 'avi_index': index,
                                    'pgmyuv_sha256': digest(pgm), 'rgba_sha256': digest(image.tobytes()),
                                    'raw_camera': frame, **camera})
    if scene_name == 'AK01':
        source_ids = (0x00024000, 0x00824000, 0x01024000, 0x08024000)
        raw = {}
        for identity in source_ids:
            payload, record = _resource(source, 'AZ01.DC', identity, int(identity == 0x08024000))
            raw[identity] = payload; info['resources'].append(record)
            (originals / f'{identity:08x}.bin').write_bytes(payload)
        parts, bones = decode_mesh(raw[0x00024000]), decode_skeleton(raw[0x00824000])
        idle = decode_animations(raw[0x01024000])[0]
        vertices = pack_pose(sample_pose(parts, bones, idle, 0))
        (output / 'ash.bin').write_bytes(vertices)
        texture, texture_info = decode_texture(raw[0x08024000], 0x08024000)
        texture.save(output / 'ash.bmp')
        # This is a declared static rendering fixture; original startup uses camera2.
        info['actor'] = {'vertex_count': len(vertices) // 32, 'parts': list(BODY_PARTS),
                         'clip': f"{idle['resource_id']:08x}", 'frame': 0, 'texture': texture_info,
                         'position': scene['navpoints'][23], 'yaw': 0,
                         'placement': 'Original navigation point23, explicit visual fixture; not a gameplay/bootstrap proof.'}
    actor = info.get('actor', {'vertex_count': 0, 'position': [0, 0, 0], 'yaw': 0})
    lines = ['EDDCVIS1', f"{actor['vertex_count']} {len(tracks)}",
             ' '.join(map(str, (*actor['position'], actor['yaw'])))]
    for track in tracks:
        camera = view_projection(track['frames'][0])
        up = [camera['view16'][row * 4 + 1] for row in range(3)]
        lines.append(' '.join(map(str, (*camera['viewProjection16'], *camera['eye3'], *camera['right3'], *up))))
    (output / 'render.txt').write_text('\n'.join(lines) + '\n', encoding='ascii')
    info['files'] = [{'path': str(p.relative_to(output)).replace('\\', '/'), 'bytes': p.stat().st_size,
                      'sha256': digest(p.read_bytes())} for p in sorted(output.rglob('*'))
                     if p.is_file() and p.name != 'INCOMPLETE']
    (output / 'profile.json').write_text(json.dumps(info, indent=2) + '\n', encoding='utf-8')
    (output / 'INCOMPLETE').unlink()
    return info


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--decoded', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--scene', choices=('AA01', 'AK01'), required=True)
    parser.add_argument('--ffmpeg', type=Path, default=shutil.which('ffmpeg'))
    args = parser.parse_args()
    if args.ffmpeg is None:
        parser.error('An existing FFmpeg executable is required for YUVJ color conversion')
    result = export_profile(args.source, args.decoded, args.output, args.scene, args.ffmpeg)
    print(f"Exported original {result['scene']} visual profile: {len(result['cameras'])} camera frames")
