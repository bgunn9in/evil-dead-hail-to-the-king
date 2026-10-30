"""Cyrillic derived solely from original Dreamcast FONT5 pixel strokes."""
from pathlib import Path
import hashlib
from PIL import Image, ImageChops
from ed_pvr import decode_pvr
RUSSIAN='АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюя«»—…№/\\'
def sha(b):return hashlib.sha256(b).hexdigest()
def blank():return Image.new('L',(18,24))

def native_font(source):
    source=Path(source).resolve(strict=True)
    raw=(source/'FONT5.PVR').read_bytes();exe=(source/'1ST_READ.BIN').read_bytes()
    if exe.count(b'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789?!.,;:+-=')!=1:raise ValueError('Missing or ambiguous native FONT5 character table')
    start=exe.index(b'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789?!.,;:+-=');end=exe.index(0,start,start+128)
    original_chars=exe[start:end].decode('ascii')
    if len(original_chars)!=88 or len(set(original_chars))!=88:raise ValueError('Original FONT5 character table differs')
    if sha(raw)!='a3fbc74f681915fd68c8a6c6a25326bad010c1a8cc18874bde8b8b2eb225107b' or sha(exe)!='de26cba0d4e6035d1128daf504a2257a695050bac11ecdf6e9e2e0baf0f26841':raise ValueError('Unsupported original Dreamcast font source')
    native=decode_pvr(raw)
    if native.size!=(256,256) or native.getchannel('R').tobytes()!=native.getchannel('A').tobytes():raise ValueError('Original FONT5 dimensions/coverage differ')
    glyph={c:native.getchannel('A').crop((n%14*18,n//14*24,n%14*18+18,n//14*24+24))for n,c in enumerate(original_chars)}
    recipes={}
    def combine(*parts):
        out=blank()
        for x in parts:out=ImageChops.lighter(out,x)
        return out
    def piece(c,box,at=None,size=None):
        patch=glyph[c].crop(box)
        if size:patch=patch.resize(size,Image.Resampling.NEAREST)
        out=blank();out.paste(patch,at or box[:2]);return out
    def cut(c,box):return piece(c,box)
    def add(c,image,recipe):glyph[c]=image;recipes[c]=recipe
    # Join native pixel rows/columns so the original serif and stem coverage are retained.
    def joined(c,box,at=None,drop_rows=(),drop_columns=()):
        xs=[x for x in range(box[0],box[2]) if x not in drop_columns]
        ys=[y for y in range(box[1],box[3]) if y not in drop_rows]
        patch=Image.new('L',(len(xs),len(ys)))
        patch.putdata([glyph[c].getpixel((x,y)) for y in ys for x in xs])
        result=blank();result.paste(patch,at or box[:2]);return result
    stem=cut('H',(2,4,6,19));right=cut('H',(12,4,16,19));bar=cut('H',(5,10,13,13));top=cut('E',(2,4,15,7))
    bottom=cut('E',(2,16,15,19));center=cut('I',(7,4,11,19))
    for a,b in zip('АВЕКМНОРСТХ','ABEKMHOPCTX'):add(a,glyph[b].copy(),'exact native '+b)
    add('Б',combine(stem,top,cut('B',(2,10,15,19))),'E left/top + B lower bowl')
    add('Г',combine(stem,top),'E/H top and left stem')
    el=glyph['A'].copy();el.paste(0,(6,12,11,14));el.paste(0,(6,14,12,15))
    add('Л',el,'native A diagonals/serifs without crossbar; no extra baseline')
    feet=combine(piece('p',(4,18,7,23),(1,18)),piece('p',(4,18,7,23),(14,18)))
    add('Д',combine(glyph['Л'],bottom,feet),'native A diagonals + E baseline + native p descender feet')
    add('Ж',combine(glyph['X'],center),'X + I center stem')
    add('З',glyph['3'].copy(),'exact native 3')
    add('И',glyph['N'].transpose(Image.Transpose.FLIP_LEFT_RIGHT),'mirrored N')
    breve=piece('U',(4,16,14,19),(4,0))
    add('Й',combine(glyph['И'],breve),'mirrored N + native U bottom curve, no resizing')
    add('П',combine(stem,right,top),'H stems + E top')
    add('У',glyph['Y'].copy(),'exact native Y')
    phiStem=combine(piece('I',(7,6,11,19),(7,1)),piece('I',(7,6,11,19),(7,10)))
    add('Ф',combine(glyph['O'],phiStem),'O + two joined I stem sections, no resizing')
    upright=combine(stem,right,bottom)
    add('Ц',combine(upright,piece('p',(4,18,7,23),(14,18))),'H stems + E baseline + native p descender')
    add('Ч',combine(cut('H',(2,4,6,12)),right,bar),'H upper left/right and middle')
    add('Ш',combine(upright,center),'H/I stems + E baseline')
    add('Щ',combine(glyph['Ш'],piece('p',(4,18,7,23),(14,18))),'H/I/E + native p descender')
    add('Ь',combine(stem,cut('B',(2,10,15,19))),'H stem + B lower bowl')
    add('Ъ',combine(glyph['Ь'],piece('E',(2,4,10,7),(0,4))),'soft-sign + E upper flag')
    add('Ы',combine(joined('Ь',(2,4,15,19),(1,4),drop_columns=(7,8,9)),piece('I',(6,4,12,19),(12,4))),'soft-sign joined counter columns + unchanged I stem')
    add('Э',combine(glyph['C'].transpose(Image.Transpose.FLIP_LEFT_RIGHT),bar),'mirrored C + H middle')
    add('Ю',combine(stem,joined('O',(2,4,16,19),(7,4),drop_columns=(6,7,8,9)),bar),'H left/middle + O joined counter columns, no resizing')
    add('Я',glyph['R'].transpose(Image.Transpose.FLIP_LEFT_RIGHT),'mirrored R')
    dots=combine(piece('.',(8,16,11,19),(4,0)),piece('.',(8,16,11,19),(11,0)))
    add('Ё',combine(glyph['E'],dots),'native E + two unchanged period dots')
    aliases=dict(zip('аеорсух','aeopcyx'))
    lower_rows={'в':(8,9,14),'г':(8,10,14),'д':(8,10,15),'з':(8,13,15),
        'и':(7,11,15),'л':(8,10,15),'м':(8,12,15),'н':(8,9,15),'п':(8,10,14),
        'т':(8,10,14),'ц':(8,10,14),'ч':(7,14,15),'ш':(8,10,14),'щ':(8,10,14),
        'ъ':(8,9,14),'ы':(8,9,14),'ь':(8,9,14),'э':(8,14,15),'ю':(8,10,14),'я':(8,9,14)}
    for c in 'абвгдеёжзийклмнопрстуфхцчшщъыьэюя':
        if c in aliases:add(c,glyph[aliases[c]].copy(),'exact native '+aliases[c]);continue
        if c=='б':
            im=combine(cut('b',(2,7,16,19)),cut('b',(4,5,7,12)),piece('f',(5,3,15,8),(3,3)));recipe='native b bowl/stem + f curved ascender, unchanged pixels'
        elif c=='ё':
            im=combine(glyph['e'],piece('.',(8,16,11,19),(4,2)),piece('.',(8,16,11,19),(11,2)));recipe='native e + unchanged period dots'
        elif c=='й':
            im=combine(glyph['и'],piece('U',(4,16,14,19),(4,3)));recipe='native-stroke и + unchanged U breve'
        elif c=='ж':
            im=combine(glyph['x'],piece('I',(7,7,11,19)));recipe='native lowercase x + unchanged I center stem'
        elif c=='к':
            im=combine(cut('n',(2,7,8,19)),cut('k',(7,7,16,19)));recipe='native n lowercase stem + k arms at original x-height'
        elif c=='ф':
            im=combine(glyph['o'],piece('p',(4,7,7,23),(8,7)),piece('l',(7,3,10,12),(8,3)));recipe='native o + p/l center stem sections, unchanged pixels'
        else:
            cap=c.upper();end=23 if c in 'дцщ' else 19
            im=joined(cap,(0,4,18,end),(0,7),drop_rows=lower_rows[c]);recipe='native-stroke '+cap+' joined to x-height preserving horizontal stroke rows '+str(lower_rows[c])
        add(c,im,recipe)
    add('«',combine(piece('<',(3,8,14,19),(1,8),(8,11)),piece('<',(3,8,14,19),(9,8),(8,11))),'two narrowed native <')
    add('»',glyph['«'].transpose(Image.Transpose.FLIP_LEFT_RIGHT),'mirrored native-stroke «')
    add('—',piece('-',(4,11,14,13),(1,11),(16,2)),'native dash stretched')
    add('…',combine(piece('.',(7,15,12,19),(1,15),(4,4)),piece('.',(7,15,12,19),(7,15),(4,4)),piece('.',(7,15,12,19),(13,15),(4,4))),'three native periods')
    add('№',combine(piece('N',(1,4,17,19),(0,4),(10,15)),piece('o',(2,7,16,19),(11,5),(6,7)),piece('-',(4,11,14,13),(11,14),(6,1))),'narrow N + native o + dash')
    add('/',piece('V',(1,4,10,19),(4,3),(10,18)).transpose(Image.Transpose.FLIP_LEFT_RIGHT),'mirrored native V left diagonal')
    add('\\',glyph['/'].transpose(Image.Transpose.FLIP_LEFT_RIGHT),'mirrored native diagonal')
    chars=original_chars+RUSSIAN;assert len(chars)==161 and len(set(chars))==len(chars)
    atlas=Image.new('RGBA',(256,512));atlas.paste(native,(0,0))
    for i,c in enumerate(RUSSIAN,88):
        a=glyph[c];assert a.getbbox(),c
        atlas.paste(Image.merge('RGBA',(a,a,a,a)),(i%14*18,i//14*24))
    for i,c in enumerate(original_chars):
        box=(i%14*18,i//14*24,i%14*18+18,i//14*24+24)
        assert atlas.crop(box).tobytes()==native.crop(box).tobytes()
    return {'atlas':atlas,'original':native,'charset':chars,'glyphs':glyph,'provenance':{'font_pvr_sha256':sha(raw),'executable_sha256':sha(exe),'charset_offset':start,'original_glyphs_preserved':88,'native_recipes':recipes,'external_font':None}}

def glyph_mask(text,font,size):
    glyph=font['glyphs']
    if size[0]<5 or size[1]<4:raise ValueError('Original label cell is too small')
    strip=Image.new('L',(max(1,len(text)*18),24))
    for i,c in enumerate(text):
        if c!=' ':strip.paste(glyph[c],(i*18,0))
    bbox=strip.getbbox()
    if not bbox:raise ValueError('Empty native glyph mask')
    strip=strip.crop(bbox);height=max(1,size[1]-3);width=min(size[0]-4,max(1,round(strip.width*height/strip.height)))
    strip=strip.resize((width,height),Image.Resampling.LANCZOS).point(lambda n:min(255,round(n/17)*17))
    result=Image.new('L',size);result.paste(strip,((size[0]-width)//2,(size[1]-height)//2));return result
