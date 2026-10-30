"""Derive Russian title/loading/GameOver pixels inside checked original atlas regions."""
import argparse
import hashlib
import json
import math
from pathlib import Path

from PIL import Image

from ed_dc_model import decode_mesh, decode_texture
from ed_objects import encode_texture
from export_dreamcast_ui import Resources
from ed_native_font import glyph_mask, native_font


TEXTURE = 0x0b942034
WORDS = ('НОВАЯ ИГРА', 'ЗАГРУЗИТЬ', 'СОХРАНИТЬ', 'НАСТРОЙКИ')
MENU_ROWS = ((10, 23), (24, 37), (38, 50), (51, 64), (70, 83), (84, 97), (98, 110), (111, 123))


def regions(parts, size):
    if size != (128, 256) or len(parts) != 8:
        raise ValueError('Original title atlas/model dimensions differ')
    result = [{'box': (64, top, 128, bottom), 'text': WORDS[n % 4], 'rotation': None,
               'selected': n < 4, 'binding': 'Original baked menu art; current MenMain renderer3 uses catalog text'}
              for n, (top, bottom) in enumerate(MENU_ROWS)]
    for n, text in enumerate(('КОНЕЦ ИГРЫ', 'ЗАГРУЗКА')):
        uv = parts[n]['uv']
        if not uv or any(len(p) != 2 or any(not math.isfinite(v) for v in p) for p in uv):
            raise ValueError('Invalid source GameOver/loading UVs')
        box = (math.floor(min(p[0] for p in uv) * size[0]), math.floor(min(p[1] for p in uv) * size[1]),
               math.ceil(max(p[0] for p in uv) * size[0]), math.ceil(max(p[1] for p in uv) * size[1]))
        result.append({'box': box, 'text': text, 'rotation': 90, 'selected': False,
                       'binding': f'Original0b142088 part{n}; loading dot parts2..7 remain untouched'})
    for n, row in enumerate(result):
        x0, y0, x1, y1 = row['box']
        if not 0 <= x0 < x1 <= size[0] or not 0 <= y0 < y1 <= size[1]:
            raise ValueError('Derived title region exceeds original atlas')
        if any(max(x0, r['box'][0]) < min(x1, r['box'][2]) and max(y0, r['box'][1]) < min(y1, r['box'][3]) for r in result[:n]):
            raise ValueError('Derived title regions overlap')
    return result


def recolor_title(original, parts, font):
    if original.mode != 'RGBA':
        raise ValueError('Original title atlas must retain RGBA')
    rows = regions(parts, original.size)
    translated = original.copy()
    allowed = Image.new('L', original.size)
    for row in rows:
        box = row['box']
        patch = original.crop(box)
        # The source text sits on transparency; preserve dark original background.
        raw = patch.tobytes()
        pixels = [tuple(raw[n:n + 4]) for n in range(0, len(raw), 4)]
        if not any(max(p[:3]) > 64 and p[3] for p in pixels):
            raise ValueError('Expected original title glyph pixels are absent')
        patch.putdata([(0, 0, 0, 0) if max(p[:3]) > 48 and p[3] else p for p in pixels])
        if row['rotation']:
            patch = patch.transpose(Image.Transpose.ROTATE_90)
        mask = glyph_mask(row['text'], font, patch.size)
        ink = Image.new('RGBA', patch.size, (255, 255, 255, 0))
        if row['selected']:
            for y in range(patch.height):
                color = (255, round(250 - 30 * y / patch.height), round(210 - 100 * y / patch.height), 0)
                for x in range(patch.width):ink.putpixel((x, y), color)
        ink.putalpha(mask)
        patch = Image.alpha_composite(patch, ink)
        if row['rotation']:
            patch = patch.transpose(Image.Transpose.ROTATE_270)
        translated.paste(patch, box[:2])
        allowed.paste(255, box)
    before, after = original.tobytes(), translated.tobytes()
    changed = 0
    for i, permitted in enumerate(allowed.tobytes()):
        if before[i * 4:i * 4 + 4] != after[i * 4:i * 4 + 4]:
            if not permitted:raise ValueError('Title localization changed protected artwork')
            changed += 1
    if not changed:raise ValueError('Title localization is empty')
    return translated, {'regions': rows, 'changed_pixels': changed, 'outside_region_changes': 0,
                        'alpha_policy': 'New transparent glyph coverage inside proven text regions; all outside RGBA bytes unchanged.'}


def localize_title(source, output):
    source, output = Path(source).resolve(strict=True), Path(output).resolve()
    if output.exists() or output == source or source in output.parents or output in source.parents:
        raise ValueError('Use a new derived overlay outside originals')
    resources = Resources(source)
    model, model_source = resources.get(('AZ01.DC', 'BZ01.DC'), 0x0b142088, 0)
    raw, texture_source = resources.get(('AZ01.DC', 'BZ01.DC'), TEXTURE, 1)
    if (hashlib.sha256(model).hexdigest() != 'f4bd1b3e900cfd540775ec2ef4c836067ac05d6bf1a48f24252d02db07fbc2bd' or
            hashlib.sha256(raw).hexdigest() != '2b3ccb113eea5c5bd8ba233da7e12f96a91a7a1a13b2c61ae0ca2dd6cdf29a09'):
        raise ValueError('Title resource version differs from the verified text/UV map')
    original, metadata = decode_texture(raw, TEXTURE)
    font = native_font(source)
    translated, stats = recolor_title(original, decode_mesh(model), font)
    encoded = encode_texture(TEXTURE, translated)
    output.mkdir(parents=True)
    for level, title in (('A', 'aa01'), ('B', 'bb01')):
        for name in (f'{level}/gui/textures/{TEXTURE:08x}.rgba', f'{level}/{title}/main-menu.texture'):
            path = output / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(encoded)
    original.save(output / 'original.png')
    translated.save(output / 'russian.png')
    proof = {'format': 'DREAMCAST-RUSSIAN-TITLE1', 'source': texture_source, 'model': model_source, 'decoded': metadata, **stats,
             'native_font': font['provenance'],
             'rgba_sha256': hashlib.sha256(translated.tobytes()).hexdigest(), 'encoded_sha256': hashlib.sha256(encoded).hexdigest(),
             'limits': ['Cyrillic derives solely from native FONT5 pixel strokes, not original Russian source pixels.', 'GameOver part0/loading part1 bound by original UVs; animated dot parts2..7 untouched.', 'Upper-right menu word cells are original baked art; current nativeMenMain renders Russian catalog text rather than those pixels.']}
    (output / 'source.json').write_text(json.dumps(proof, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return proof


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    print(json.dumps(localize_title(args.source, args.output), ensure_ascii=True, indent=2))
