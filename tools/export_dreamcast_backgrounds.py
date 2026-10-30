"""Decode every original TM2X AVI to native RGB320x240 with explicit mapping gaps."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
ROOT = Path(__file__).resolve().parents[1]
from ed_assets import parse_dc_archive
from ed_camera import parse_scene
from export_dreamcast_visual import read_pgmyuv
from PIL import Image
from ed_dc_audio import checked_output as checked_original_output
DECODER = ROOT / 'build/dependencies/nihav-tm2x-20261027/target/release/nihav-tool.exe'
DECODER_SHA = '4e8d95ae32b53be4bc1f65fe9fe957a8f5392fac97388b178def2c5598964e6c'
FFMPEG_SHA = 'b1383f5d07470d503edecdaee4bddc5891e986e916a698299b357f79cfe445fd'
SOURCE_MANIFEST_SHA = 'cb0c24fee804eb7cc7bc65f4e2311f7498ab1c075f12f7c8cc7f5ff1d4cbc9c9'
LICENSE = ROOT / 'build/dependencies/nihav-tm2x-20261027/src/COPYING'
LICENSE_SHA = 'e0eedba615d5cd1b986afb6c5b3a4b1ae33713e7e9dc74d19daec5e3221f9d2e'
FRAMEWORK_COMMIT = '13f31633cf325b0036d697af7bf05c611b4b8967'
TOOL_COMMIT = 'f80bd9c6adbc7a1c6f13c391717199fd0a19cf55'
FRAMEWORK_BUNDLE_SHA = 'a86092a48e461a34b3a68bfa273e55ef9c7efafc098bef0e694b337933f3e573'
TOOL_BUNDLE_SHA = '0b88b75733403df4c3531b8f445882ad59cba44d009aa3c5f94036e13ee7c7af'
NO_WINDOW = getattr(subprocess, 'CREATE_NO_WINDOW', 0)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def chunks(data, start, end):
    while start < end:
        if end-start < 8:
            raise ValueError('Truncated RIFF chunk header')
        tag, size = struct.unpack_from('<4sI', data, start)
        at = start+8
        if size > end-at:
            raise ValueError('RIFF chunk exceeds parent')
        yield tag, at, size
        start = at+size+(size & 1)
    if start != end:
        raise ValueError('RIFF alignment exceeds parent')

def inspect_avi(data):
    if len(data) < 12 or data[:4] != b'RIFF' or data[8:12] != b'AVI ' or struct.unpack_from('<I', data, 4)[0]+8 != len(data):
        raise ValueError('Invalid original AVI allocation')
    main, streams, packets = [], [], []
    def walk(start, end, scope=b''):
        for tag, at, size in chunks(data, start, end):
            if tag == b'LIST':
                if size < 4:
                    raise ValueError('Truncated AVI LIST type')
                walk(at+4, at+size, data[at:at+4])
            elif tag == b'avih':
                if size < 40:
                    raise ValueError('Truncated AVI main header')
                main.append(struct.unpack_from('<10I', data, at))
            elif tag == b'strh' and size >= 4 and data[at:at+4] == b'vids':
                if size < 56:
                    raise ValueError('Truncated AVI video stream')
                streams.append({'handler': data[at+4:at+8].decode('ascii'), 'scale': struct.unpack_from('<I', data, at+20)[0],
                                'rate': struct.unpack_from('<I', data, at+24)[0], 'length': struct.unpack_from('<I', data, at+32)[0]})
            elif scope in (b'movi', b'rec ') and tag in (b'00dc', b'00db'):
                packets.append((at, size))
    walk(12, len(data))
    if len(main) != 1 or len(streams) != 1 or not packets:
        raise ValueError('Expected one original AVI video stream and header')
    if main[0][8:10] != (320, 240) or streams[0]['handler'] != 'TM2X' or not streams[0]['rate'] or not streams[0]['scale']:
        raise ValueError('Unexpected original TM2X dimensions/rate')
    if len(packets) != main[0][4] or len(packets) != streams[0]['length']:
        raise ValueError('AVI physical/declared frame count differs')
    return {'width': 320, 'height': 240, 'packet_count': len(packets), 'stream': streams[0],
            'packet_spans': [{'offset': at, 'bytes': size} for at, size in packets]}

def convert_rgb(planes, ffmpeg):
    command = [str(ffmpeg), '-v', 'error', '-threads', '1', '-filter_threads', '1', '-f', 'rawvideo',
               '-pixel_format', 'yuvj444p', '-video_size', '320x240', '-i', 'pipe:0',
               '-frames:v', str(len(planes)), '-sws_flags', 'accurate_rnd+bitexact', '-threads', '1',
               '-f', 'rawvideo', '-pix_fmt', 'rgb24', 'pipe:1']
    converted = subprocess.run(command, input=b''.join(planes), capture_output=True, timeout=300, creationflags=NO_WINDOW)
    if converted.returncode or len(converted.stdout) != len(planes)*320*240*3:
        raise ValueError('Native frame color conversion failed: '+converted.stderr[:1024].decode(errors='replace'))
    return converted.stdout, command

def mapping_gap(filename, avi_count, dcv_count, indices):
    if dcv_count == avi_count and all(i < avi_count for i in indices):
        return None
    return {'file': filename, 'avi_frames': avi_count, 'dcv_backgrounds': dcv_count,
            'camera_frames': len(indices), 'max_camera_background_index': max(indices, default=None),
            'reason': 'Original physical counts/indexes disagree; no aliases guessed.'}

def write_json(path, value):
    pending = path.with_suffix(path.suffix+'.pending')
    pending.write_text(json.dumps(value, indent=2)+'\n', encoding='utf-8')
    os.replace(pending, path)

def checked_inputs(source, only=None):
    manifest=(source.parent/'files-manifest.json').read_bytes()
    if sha(manifest)!=SOURCE_MANIFEST_SHA:
        raise ValueError('Unknown original Dreamcast extraction manifest')
    originals={f['path']:f['sha256'] for f in json.loads(manifest)['files']}
    names={name for name in originals if name.endswith('.AVI')}
    if len(names)!=62 or {p.name for p in source.glob('*.AVI')}!=names:
        raise ValueError('Expected the complete62 original AVI filenames, without unknown additions')
    selected=names if only is None else set(only)
    if not selected or not selected<=names:
        raise ValueError('Unknown original Dreamcast AVI selection')
    for name in sorted(selected):
        for asset in (name,name[:-4]+'V.DC',name[:-4]+'.DC'):
            if sha((source/asset).read_bytes())!=originals[asset]:
                raise ValueError('Original resource hash differs: '+asset)
    return originals,[source/name for name in sorted(selected)]

def checked_tools(decoder,ffmpeg,license_path):
    for path,expected,name in ((decoder,DECODER_SHA,'NihAV decoder'),(ffmpeg,FFMPEG_SHA,'FFmpeg converter'),(license_path,LICENSE_SHA,'NihAV AGPL license')):
        if sha(path.read_bytes())!=expected:raise ValueError('Pinned '+name+' hash differs')

def export(source, output, ffmpeg, *, decoder=DECODER, license_path=LICENSE, only=None):
    """Create one new cache atomically; an optional subset is proof-only.

    Existing caches and original resources are never modified. Decoder processes
    are external offline tools; none is copied or linked into the game runtime.
    """
    source, output, ffmpeg = Path(source).resolve(), Path(output).resolve(), Path(ffmpeg).resolve()
    decoder,license_path=Path(decoder).resolve(),Path(license_path).resolve()
    checked_original_output(source,output)
    if output == source or source in output.parents or output in source.parents:
        raise ValueError('Native background output must be outside immutable input')
    if output.exists():
        raise FileExistsError(output)
    originals,paths=checked_inputs(source,only)
    checked_tools(decoder,ffmpeg,license_path)
    output.parent.mkdir(parents=True,exist_ok=True)
    staging=Path(tempfile.mkdtemp(prefix='.'+output.name+'.pending-',dir=output.parent)).resolve()
    # The only recursive cleanup target is the exact fresh sibling we created.
    if staging.parent!=output.parent or not staging.name.startswith('.'+output.name+'.pending-'):
        raise ValueError('Generated staging directory escaped output parent')
    try:
        result=_export_into(source,staging,ffmpeg,decoder,license_path,originals,paths,only is None)
        if result['conversion_status'] not in ('PASS','SUBSET_PASS'):
            raise ValueError('Native background conversion failed: '+json.dumps(result['errors']))
        if output.exists():raise FileExistsError(output)
        os.rename(staging,output)
        return result
    finally:
        if staging.exists():shutil.rmtree(staging)

def _export_into(source,output,ffmpeg,decoder,license_path,originals,paths,complete):
    (output / 'INCOMPLETE').write_text('Original TM2X export has not completed.\n')
    shutil.copyfile(license_path, output / 'NihAV-COPYING')
    version = subprocess.run([str(ffmpeg), '-version'], capture_output=True, timeout=20, creationflags=NO_WINDOW)
    if version.returncode:
        raise ValueError('Cannot identify external FFmpeg converter')
    result = {'format': 'EDDCTM2XNATIVE1', 'conversion_status': 'RUNNING', 'mapping_status': 'PARTIAL', 'original_binary_executed': False,
              'native_size': [320, 240], 'color': 'NihAV full-range YUVJ444, FFmpeg accurate_rnd+bitexact RGB24; no scaling.',
              'source_manifest_sha256':SOURCE_MANIFEST_SHA,'complete_original_set':complete,
              'decoder': {'path': str(decoder), 'sha256': DECODER_SHA, 'license': 'AGPL3 external build-time process only',
                  'framework_commit':FRAMEWORK_COMMIT,'tool_commit':TOOL_COMMIT,'framework_bundle_sha256':FRAMEWORK_BUNDLE_SHA,
                  'tool_bundle_sha256':TOOL_BUNDLE_SHA,'license_sha256':LICENSE_SHA},
              'ffmpeg': {'path': str(ffmpeg), 'sha256': sha(ffmpeg.read_bytes()), 'version': version.stdout.decode(errors='replace').splitlines()[0]},
              'archives': [], 'decoded_frames': 0, 'mapping_gaps': [], 'errors': []}
    started = time.monotonic()
    for path in paths:
        at = time.monotonic()
        try:
            raw = path.read_bytes()
            if sha(raw) != originals[path.name]:
                raise ValueError('Original AVI hash differs from extraction manifest')
            avi = inspect_avi(raw)
            dcv_path = source / (path.stem+'V.DC')
            dcv = dcv_path.read_bytes()
            if sha(dcv) != originals[dcv_path.name]:
                raise ValueError('Original DCV hash differs from extraction manifest')
            entries = [e for e in parse_dc_archive(dcv) if e.resource_id >> 24 == 8 and not e.flags]
            scene_path = source / (path.stem+'.DC')
            scene_raw = scene_path.read_bytes()
            if sha(scene_raw) != originals[scene_path.name]:
                raise ValueError('Original scene hash differs from extraction manifest')
            native_scene = [e for e in parse_dc_archive(scene_raw) if e.resource_id >> 24 == 15 and not e.flags]
            if len(native_scene) != 1:
                raise ValueError('Expected one original native scene')
            scene = parse_scene(scene_raw[native_scene[0].offset:native_scene[0].offset+native_scene[0].size])
            indices = [f['background_index'] for t in scene['tracks'] for f in t['frames']]
            gap = mapping_gap(path.name, avi['packet_count'], len(entries), indices)
            mapped = gap is None
            if gap:
                result['mapping_gaps'].append(gap)
            destination = output / path.stem
            decoded = destination / 'pgmyuv'; decoded.mkdir(parents=True)
            rgb_dir = destination / 'native-rgb'; rgb_dir.mkdir()
            command = [str(decoder), '-an', '-nm=count', '-vpfx', str(decoded / 'frame'), str(path)]
            decode = subprocess.run(command, capture_output=True, timeout=300, creationflags=NO_WINDOW)
            log = decode.stdout+decode.stderr
            (destination / 'decode.log').write_bytes(log)
            if decode.returncode or b'error' in log.lower() or b'panic' in log.lower() or b'truemotion2x' not in log:
                raise ValueError('External decoder failed: '+log[:1024].decode(errors='replace'))
            frames = sorted(decoded.glob('*.pgm'))
            if len(frames) != avi['packet_count'] or any(p.name != f'frame00_{i+1:06d}.pgm' for i, p in enumerate(frames)):
                raise ValueError('Decoded original AVI frame count/sequence differs')
            pgms = [p.read_bytes() for p in frames]
            pixels, convert_command = convert_rgb([read_pgmyuv(pgm) for pgm in pgms], ffmpeg)
            records = []
            for i, (frame, pgm) in enumerate(zip(frames, pgms)):
                rgb = pixels[i*320*240*3:(i+1)*320*240*3]
                image = Image.frombytes('RGB', (320, 240), rgb)
                target = rgb_dir / f'frame_{i:06d}.bmp'; image.save(target)
                entry = entries[i] if mapped else None
                records.append({'avi_index': i, 'pgmyuv': frame.name, 'pgmyuv_sha256': sha(pgm), 'rgb_sha256': sha(rgb),
                                'native_bmp': target.name, 'bmp_sha256': sha(target.read_bytes()),
                                'dcv_physical_entry': entry.index if entry else None,
                                'dcv_id': f'{entry.resource_id:08x}' if entry else None})
                if i in (0, len(frames)//2, len(frames)-1):
                    image.save(destination / f'check-{i:06d}.png')
            record = {'file': path.name, 'source_sha256': sha(raw), 'dcv_sha256': sha(dcv), 'scene_sha256': sha(scene_raw),
                      'avi': avi, 'camera_tracks': scene['tracks'], 'structural_avi_dcv_mapping': mapped,
                      'mapping_scope': 'Equal physical counts and in-range source camera indexes only; native ordering beyond AA01/AK01 needs independent validation.',
                      'decode_command': command, 'color_command': convert_command, 'elapsed_seconds': time.monotonic()-at, 'frames': records}
            write_json(destination / 'source.json', record)
            result['decoded_frames'] += len(frames)
            result['archives'].append({'file': path.name, 'source_sha256': record['source_sha256'], 'decoded_frames': len(frames),
                                       'mapping': mapped, 'manifest_sha256': sha((destination / 'source.json').read_bytes())})
            print(path.name, len(frames), 'native frames;', 'structural mapping PASS' if mapped else 'mapping GAP', flush=True)
        except Exception as error:
            result['errors'].append({'file': path.name, 'error': str(error)})
            print(path.name, 'ERROR:', error, flush=True)
            break
        result['elapsed_seconds'] = time.monotonic()-started
        write_json(output / 'source.json', result)
    result['elapsed_seconds']=time.monotonic()-started
    result['conversion_status'] = ('PASS' if complete else 'SUBSET_PASS') if len(result['archives']) == len(paths) and not result['errors'] else 'INCOMPLETE'
    result['mapping_status'] = 'PASS' if not result['mapping_gaps'] and not result['errors'] else 'PARTIAL'
    write_json(output / 'source.json', result)
    if result['conversion_status'] in ('PASS','SUBSET_PASS'):
        (output / 'INCOMPLETE').unlink()
    print(json.dumps({k: result[k] for k in ('conversion_status', 'mapping_status', 'decoded_frames', 'mapping_gaps', 'errors', 'elapsed_seconds')}, indent=2), flush=True)
    return result

def verify_cache(source,cache):
    """Verify an existing complete cache without rewriting or decoding any frame."""
    source,cache=Path(source).resolve(),Path(cache).resolve()
    checked_original_output(source,cache)
    originals,paths=checked_inputs(source)
    manifest=json.loads((cache/'source.json').read_text())
    if (manifest.get('format')!='EDDCTM2XNATIVE1' or manifest.get('conversion_status')!='PASS'
        or manifest.get('native_size')!=[320,240] or manifest['decoder']['sha256']!=DECODER_SHA
        or manifest['ffmpeg']['sha256']!=FFMPEG_SHA or (cache/'INCOMPLETE').exists()):
        raise ValueError('Expected the complete pinned native background cache')
    if sha((cache/'NihAV-COPYING').read_bytes())!=LICENSE_SHA:raise ValueError('Native cache license changed')
    archives=manifest['archives']
    if len(archives)!=62 or {a['file'] for a in archives}!={p.name for p in paths}:raise ValueError('Incomplete native AVI cache catalog')
    frames=0
    for archive in archives:
        name=archive['file'];directory=cache/Path(name).stem
        encoded=(directory/'source.json').read_bytes()
        if sha(encoded)!=archive['manifest_sha256']:raise ValueError('Native AVI manifest changed: '+name)
        item=json.loads(encoded)
        for field,asset in (('source_sha256',name),('dcv_sha256',name[:-4]+'V.DC'),('scene_sha256',name[:-4]+'.DC')):
            if item[field]!=originals[asset]:raise ValueError('Native cache source identity differs: '+asset)
        rows=item['frames']
        if len(rows)!=archive['decoded_frames'] or len(rows)!=item['avi']['packet_count']:raise ValueError('Native cache frame count changed')
        for i,row in enumerate(rows):
            if row['avi_index']!=i or row['native_bmp']!=f'frame_{i:06d}.bmp' or row['pgmyuv']!=f'frame00_{i+1:06d}.pgm':raise ValueError('Native cache frame sequence changed')
            bmp=(directory/'native-rgb'/row['native_bmp']).read_bytes();pgm=(directory/'pgmyuv'/row['pgmyuv']).read_bytes()
            if sha(bmp)!=row['bmp_sha256'] or sha(pgm)!=row['pgmyuv_sha256']:raise ValueError('Native cache frame bytes changed')
            read_pgmyuv(pgm)
            with Image.open(directory/'native-rgb'/row['native_bmp']) as image:
                if image.format!='BMP' or image.size!=(320,240) or image.mode!='RGB' or sha(image.tobytes())!=row['rgb_sha256']:
                    raise ValueError('Native cache RGB shape/pixels changed')
            frames+=1
    if frames!=manifest['decoded_frames']:raise ValueError('Native cache total changed')
    return dict(status='PASS',archives=len(archives),frames=frames,rewritten_files=0,decoded_frames=0,manifest_sha256=sha((cache/'source.json').read_bytes()))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--ffmpeg', default=shutil.which('ffmpeg'), type=Path)
    parser.add_argument('--decoder',default=DECODER,type=Path)
    parser.add_argument('--nihav-license',default=LICENSE,type=Path)
    parser.add_argument('--only',action='append',help='Decode a named original AVI into a proof-only SUBSET_PASS cache; repeat to select several.')
    parser.add_argument('--verify-cache',action='store_true',help='Read and verify an existing complete output cache without decoding or writing.')
    args = parser.parse_args()
    if args.verify_cache:
        if args.only:parser.error('--only cannot be combined with --verify-cache')
        print(json.dumps(verify_cache(args.source,args.output)));sys.exit(0)
    if not args.ffmpeg:
        parser.error('Installed FFmpeg is required')
    result=export(args.source,args.output,args.ffmpeg,decoder=args.decoder,license_path=args.nihav_license,only=args.only)
    sys.exit(0 if result['conversion_status'] in ('PASS','SUBSET_PASS') else 1)
