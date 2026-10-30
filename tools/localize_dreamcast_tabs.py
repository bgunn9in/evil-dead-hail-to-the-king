"""Create a derived Russian tab atlas inside the original Dreamcast Maya UV cells."""
import argparse
import hashlib
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw
from ed_native_font import glyph_mask, native_font

from ed_dc_model import decode_mesh, decode_texture
from ed_objects import encode_texture
from export_dreamcast_ui import Resources


TEXTURE = 0x0b942054
MODEL = 0x0b142044
LABELS = ('ВЕЩИ', 'ТЕКСТ', 'КАРТА', 'ОПЦИИ', 'СУНДУК', 'ЗАПИСЬ')
PART_LABEL = (*range(6), *range(6), 4, 5)


def cells(parts, size):
    if len(parts) != 14:
        raise ValueError('Original inventory tab model must contain14 source states')
    width, height = size
    result = []
    for part in parts:
        uv = part['uv']
        if not uv or any(len(p) != 2 or any(not math.isfinite(v) for v in p) for p in uv):
            raise ValueError('Invalid original tab UV coordinates')
        box = (math.floor(min(p[0] for p in uv) * width), math.floor(min(p[1] for p in uv) * height),
               math.ceil(max(p[0] for p in uv) * width), math.ceil(max(p[1] for p in uv) * height))
        if not 1 <= box[0] < box[2] < width - 1 or not 1 <= box[1] < box[3] < height - 1:
            raise ValueError('Original tab cell exceeds source atlas or paper margin')
        if any(max(box[0], x0) < min(box[2], x1) and max(box[1], y0) < min(box[3], y1) for x0, y0, x1, y1 in result):
            raise ValueError('Original tab UV cells overlap')
        result.append(box)
    return result


def paper(image, box, vertical):
    """Recover only the label interior from its adjacent original paper rows."""
    x0, y0, x1, y1 = box
    patch = image.crop(box)
    for y in range(patch.height):
        for x in range(patch.width):
            if vertical:
                a, b = image.getpixel((x0 - 1, y0 + y)), image.getpixel((x1, y0 + y))
                t = (x + 1) / (patch.width + 1)
            else:
                a, b = image.getpixel((x0 + x, y0 - 1)), image.getpixel((x0 + x, y1))
                t = (y + 1) / (patch.height + 1)
            # Preserve source alpha exactly; only text-bearing RGB is derived.
            patch.putpixel((x, y), tuple(round(a[n] * (1 - t) + b[n] * t) for n in range(3)) + (patch.getpixel((x, y))[3],))
    return patch


def recolor_tabs(original, parts, font):
    if original.mode != 'RGBA':
        raise ValueError('Tab atlas must retain original RGBA pixels')
    regions = cells(parts, original.size)
    result = original.copy()
    records = []
    for index, (box, label) in enumerate(zip(regions, PART_LABEL)):
        vertical = index in (3, 9)
        patch = paper(original, box, vertical)
        if vertical:
            patch = patch.transpose(Image.Transpose.ROTATE_270)
        mask = glyph_mask(LABELS[label], font, patch.size)
        selected, disabled = 6 <= index <= 11, index >= 12
        ink = Image.new('RGBA', patch.size)
        draw = ImageDraw.Draw(ink)
        for y in range(patch.height):
            t = y / max(1, patch.height - 1)
            if selected:
                color = (255, round(250 - 30 * t), round(220 - 90 * t), 255)
            elif disabled:
                color = (115, 108, 87, 255)
            else:
                color = (20, 17, 12, 255)
            draw.line((0, y, patch.width, y), fill=color)
        alpha = patch.getchannel('A')
        patch = Image.composite(ink, patch, mask)
        patch.putalpha(alpha)
        if vertical:
            patch = patch.transpose(Image.Transpose.ROTATE_90)
        result.paste(patch, box[:2])
        records.append({'part': index, 'label': LABELS[label], 'box': list(box), 'vertical': vertical,
                        'state': 'selected' if selected else 'disabled' if disabled else 'normal'})
    allowed = Image.new('L', original.size)
    for box in regions:
        ImageDraw.Draw(allowed).rectangle((box[0], box[1], box[2] - 1, box[3] - 1), fill=255)
    outside = changed = 0
    before, after = original.tobytes(), result.tobytes()
    for index, permitted in enumerate(allowed.tobytes()):
        if before[index * 4:index * 4 + 4] != after[index * 4:index * 4 + 4]:
            changed += 1
            outside += not permitted
    if outside or result.getchannel('A').tobytes() != original.getchannel('A').tobytes() or not changed:
        raise ValueError('Derived tabs changed protected pixels/alpha or contain no edits')
    return result, {'changed_pixels': changed, 'outside_cell_changes': outside, 'alpha_byte_exact': True, 'cells': records}


def localize_tabs(source, output):
    source, output = Path(source).resolve(strict=True), Path(output).resolve()
    if output.exists() or output == source or source in output.parents or output in source.parents:
        raise ValueError('Use a new derived overlay outside original files')
    resources = Resources(source)
    model, model_source = resources.get(('AZ01.DC', 'BZ01.DC'), MODEL, 0)
    raw, texture_source = resources.get(('AZ01.DC', 'BZ01.DC'), TEXTURE, 1)
    if (hashlib.sha256(model).hexdigest() != '39f874583c99a2261a0a68a20c65fcc8217d8528b96077934c66becbe21e3e9a' or
            hashlib.sha256(raw).hexdigest() != 'c2c013f31a2a69235e53dd9305f8f4a0d5f182117f3232b6b9bf60e390cddac4'):
        raise ValueError('Original tab resource version differs from the verified UV/label map')
    original, metadata = decode_texture(raw, TEXTURE)
    font = native_font(source)
    translated, stats = recolor_tabs(original, decode_mesh(model), font)
    encoded = encode_texture(TEXTURE, translated)
    output.mkdir(parents=True)
    target = output / f'gui/textures/{TEXTURE:08x}.rgba'
    target.parent.mkdir(parents=True)
    target.write_bytes(encoded)
    original.save(output / 'original.png')
    translated.save(output / 'russian.png')
    proof = {'format': 'DREAMCAST-RUSSIAN-TABS1', 'scope': 'Derived Russian text in original tab UV cells; original model, UVs, icons, portraits, all outside pixels and all alpha remain unchanged.',
             'model': model_source, 'texture': texture_source, 'decoded': metadata, **stats,
             'native_font': font['provenance'],
             'rgba_sha256': hashlib.sha256(translated.tobytes()).hexdigest(), 'encoded_sha256': hashlib.sha256(encoded).hexdigest(),
             'style_limit': 'Russian glyphs derive solely from native FONT5 strokes; label-interior paper interpolates adjacent original rows, not recovered Russian source artwork.'}
    (output / 'source.json').write_text(json.dumps(proof, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return proof


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    print(json.dumps(localize_tabs(args.source, args.output), ensure_ascii=True, indent=2))
