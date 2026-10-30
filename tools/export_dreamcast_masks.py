"""Export original DCV masks for importer review, outside immutable inputs."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from ed_assets import parse_dc_archive
from ed_dc_masks import decode_dc_masks, metadata

def render_table(layers):
    lines = ['EDDCMASKDRAW1', str(len(layers))]
    for layer in layers:
        values = [*layer['screen_rect_xywh'], layer['effective_distance'], layer['native_inverse_z'],
                  layer['native_rgb_factor'], *layer['local_uv_bounds'][2:]]
        lines.append(' '.join(map(str, values)))
    return '\n'.join(lines) + '\n'

def write_layers(layers, output, suffix):
    """Suffix is the eventual camera-track[_frame] index, not a resource ID."""
    if not re.fullmatch(r'\d+(?:_\d+)?', suffix):
        raise ValueError('Expected numeric camera/frame suffix')
    output = Path(output)
    table = output / f'background_{suffix}.masks'
    targets = [table, *(output / f'mask_{suffix}_{layer["index"]}.coverage8' for layer in layers)]
    for target in targets:
        if target.exists():
            raise FileExistsError(target)
    for layer in layers:
        (output / f'mask_{suffix}_{layer["index"]}.coverage8').write_bytes(layer['native_texture_coverage'])
    table.write_text(render_table(layers), encoding='ascii')

def export_sources(source, output):
    source, output = Path(source).resolve(), Path(output).resolve()
    if output == source or source in output.parents or output in source.parents:
        raise ValueError('Mask export must be outside its immutable source directory')
    if output.exists():
        raise FileExistsError(output)
    archives = sorted(source.glob('*V.DC'))
    if not archives:
        raise ValueError('No original DCV archives')
    output.mkdir(parents=True)
    (output / 'INCOMPLETE').write_text('Native mask export is incomplete.\n', encoding='ascii')
    result = {'format': 'EDDCCOVERAGESET1', 'scope': 'Native layer facts+bounded texture, no PVR raster-equivalence claim.',
              'presentation_size': [640, 480], 'texture_size': [256, 256], 'archives': [],
              'backgrounds': 0, 'layers': 0, 'overscan_pitch65': 0, 'height_byte_zero': 0, 'distance_fallbacks': []}
    for path in archives:
        data = path.read_bytes()
        records = []
        destination = output / path.stem
        destination.mkdir()
        for entry in parse_dc_archive(data):
            if entry.resource_id >> 24 != 8 or entry.flags != 0:
                continue
            payload = data[entry.offset:entry.offset+entry.size]
            layers = decode_dc_masks(payload)
            ordinal = len(records)
            write_layers(layers, destination, str(ordinal))
            record = metadata({'ordinal': ordinal, 'entry': entry.index, 'id': entry.resource_id,
                               'offset': entry.offset, 'bytes': entry.size, 'sha256': hashlib.sha256(payload).hexdigest(),
                               'layers': layers})
            records.append(record)
            for layer in layers:
                stem = f'source_{ordinal}_{layer["index"]}'
                (destination / f'{stem}.2bpp').write_bytes(layer['raw'])
                # Retain the exact source rows; pitch65 overlaps native rows.
                (destination / f'{stem}.expanded.coverage8').write_bytes(layer['coverage'])
                if layer['native_write_spill']:
                    (destination / f'{stem}.spill.coverage8').write_bytes(layer['native_write_spill'])
                result['layers'] += 1
                result['overscan_pitch65'] += layer['pitch_bytes'] == 65
                result['height_byte_zero'] += layer['height_byte'] == 0
                if layer['distance'] != layer['effective_distance']:
                    result['distance_fallbacks'].append([path.name, entry.index, layer['index'], layer['distance_bits']])
        result['backgrounds'] += len(records)
        archive_record = {'archive': path.name, 'sha256': hashlib.sha256(data).hexdigest(), 'records': records}
        (destination / 'source.json').write_text(json.dumps(archive_record, indent=2)+'\n', encoding='utf-8')
        result['archives'].append({'archive': path.name, 'sha256': archive_record['sha256'], 'backgrounds': len(records)})
    (output / 'source-masks.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    (output / 'INCOMPLETE').unlink()
    return result

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = export_sources(args.source, args.output)
    print(json.dumps({k: v for k, v in result.items() if k != 'archives'}, indent=2))
