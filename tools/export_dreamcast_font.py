"""Export Dreamcast FONT5 with Cyrillic derived only from its native pixel strokes."""
import argparse
import hashlib
import json
from pathlib import Path

from ed_native_font import RUSSIAN, native_font


def export_font(source, output):
    source, output = Path(source).resolve(strict=True), Path(output).resolve()
    if output.exists() or output == source or source.is_relative_to(output) or output.is_relative_to(source):
        raise ValueError('Font export requires a new directory outside extracted originals')
    data = native_font(source)
    image, charset = data['atlas'], data['charset']
    output.mkdir(parents=True)
    image.save(output / 'font.bmp')
    (output / 'font.txt').write_bytes(b'EDFONT2\n18 24 14 256 512\nCP1251\n' + charset.encode('cp1251') + b'\n')
    proof = {'format': 'EDDREAMCASTFONT1', 'source': str(source), 'characters': len(charset),
             'charset': charset, 'atlas': list(image.size), 'cell': [18, 24], 'columns': 14,
             **data['provenance'], 'rgba_sha256': hashlib.sha256(image.tobytes()).hexdigest(),
             'scope': 'Native FONT5 cells88 byte-exact; Cyrillic derives from original glyph strokes, flips and row/column joins without whole-letter rescaling. Punctuation uses original stroke scaling. No external font.'}
    (output / 'source.json').write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8')
    return proof


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    print(json.dumps(export_font(args.source, args.output)))
