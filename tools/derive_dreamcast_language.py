"""Seal a fresh bilingual derivative; original profile files are immutable hardlinks."""
import argparse
import os
from pathlib import Path
import struct

import import_dreamcast_runtime as imp

BASE='provenance/language-base.json'
OVERLAY='provenance/language-overlay.json'
EXTRA=tuple(prefix+'gui/languages/en/'+name for prefix in ('','levelB/')
            for name in ('0b942034.rgba','0b942054.rgba','inventory.bin'))


def inventory_shape(data,english=False):
    if len(data)<20 or data[:8]!=b'EDINV2\0\0':raise ValueError('Invalid bilingual inventory')
    encoding,textures,items=struct.unpack_from('<3I',data,8)
    if encoding!=1251 or not 0<textures<=8 or items!=121:raise ValueError('Bilingual inventory count/encoding differs')
    at=20+textures*12
    if at>len(data):raise ValueError('Truncated bilingual atlas table')
    shape=bytearray(data[:at]);seen=set()
    for _ in range(items):
        if at+44>len(data):raise ValueError('Truncated bilingual item record')
        identity,_,atlas,_=struct.unpack_from('<4I',data,at)
        if identity in seen or atlas>=textures:raise ValueError('Invalid bilingual item identity/atlas')
        seen.add(identity);shape+=data[at:at+36]
        name,description=struct.unpack_from('<2I',data,at+36);at+=44
        if not 0<name<=256 or not 0<description<=4096 or at+name+description>len(data):raise ValueError('Invalid bilingual label bounds')
        labels=data[at:at+name+description]
        if english and any(c>126 or c<32 and c!=10 for c in labels):raise ValueError('English sidecar contains non-ASCII labels')
        at+=name+description
    if at!=len(data):raise ValueError('Trailing bilingual inventory bytes')
    return bytes(shape)


def verify_derivation(assets,manifest):
    receipt=manifest['language_sidecars']
    if receipt.get('format')!='EDDREAMCAST-LANGUAGES1' or receipt.get('base_manifest_path')!=BASE or receipt.get('overlay_manifest_path')!=OVERLAY:
        raise ValueError('Unknown bilingual derivation')
    raw=(assets/BASE).read_bytes();overlay_raw=(assets/OVERLAY).read_bytes()
    if imp.digest(raw)!=receipt['base_manifest_sha256'] or imp.digest(overlay_raw)!=receipt['overlay_manifest_sha256']:
        raise ValueError('Bilingual provenance identity differs')
    base=imp.load(assets/BASE);overlay=imp.load(assets/OVERLAY)
    if (base.get('format')!=imp.FORMAT or base.get('status')!='verified-import' or 'language_sidecars' in base or
        base['source_manifest_sha256']!=imp.SOURCE_SHA or base['inputs_manifest_sha256']!=manifest['inputs_manifest_sha256'] or
        imp.digest(imp.json_bytes(base['files']))!=base['files_sha256']):
        raise ValueError('Invalid bilingual base seal')
    if (overlay.get('format')!='EDDREAMCAST-LANGUAGES1' or overlay['source_manifest_sha256']!=imp.SOURCE_SHA or
        overlay['inventory_items']!=121 or overlay['languages']!=['en','ru']):
        raise ValueError('Invalid original English sidecar manifest')
    before={r['path']:r for r in base['files']};after={r['path']:r for r in manifest['files']}
    rows={r['path']:r for r in overlay['files']}
    if len(rows)!=6 or set(rows)!=set(EXTRA) or set(after)!=set(before)|set(EXTRA)|{BASE,OVERLAY}:
        raise ValueError('Bilingual derivative changed resource identities')
    if any(after[name]!=row for name,row in before.items()):
        raise ValueError('Bilingual derivative modified existing resources')
    for name,row in rows.items():
        file=assets/name;data=file.read_bytes()
        if len(data)!=row['bytes'] or imp.digest(data)!=row['sha256'] or any(after[name][key]!=row[key] for key in ('bytes','sha256')):
            raise ValueError('Bilingual sidecar bytes differ: '+name)
        if name.endswith('.rgba'):
            identity=int(Path(name).stem,16);dims=(128,256) if identity==0x0b942034 else (256,512)
            if data[:8]!=b'EDTEX1\0\0' or struct.unpack_from('<3I',data,8)!=(identity,*dims) or len(data)!=20+dims[0]*dims[1]*4:
                raise ValueError('Original English atlas identity/dimensions differ')
            if row['source']['id']!=f'{identity:08x}' or row['source']['flags']!=1:
                raise ValueError('Original English atlas source differs')
        else:
            original='levelB/inventory.bin' if name.startswith('levelB/') else 'inventory.bin'
            if (row['base_inventory_sha256']!=before[original]['sha256'] or
                    inventory_shape(data,True)!=inventory_shape((assets/original).read_bytes())):
                raise ValueError('English inventory changed source geometry/IDs')
    return base


def derive(base,overlay,output,expected_base):
    base,overlay=imp.checked_path(base),imp.checked_path(overlay)
    output=imp.output_guard(output,(base,overlay))
    marker=base/imp.MARKER;imp.no_reparse(marker)
    raw=marker.read_bytes();manifest=imp.load(marker)
    if imp.digest(raw)!=expected_base or manifest.get('status')!='verified-import' or 'language_sidecars' in manifest:
        raise ValueError('Unexpected immutable source profile')
    overlay_raw=(overlay/'manifest.json').read_bytes();language=imp.load(overlay/'manifest.json')
    imp.checked_original_output(imp.checked_path(language['source']),output)
    names=[imp.relative(r['path']) for r in manifest['files']]
    if len(names)!=len(set(names)) or len(names)!=manifest['file_count'] or imp.digest(imp.json_bytes(manifest['files']))!=manifest['files_sha256']:
        raise ValueError('Invalid source profile file table')
    present=set()
    for path in base.rglob('*'):
        imp.no_reparse(path)
        if path.is_file() and path!=marker:present.add(path.relative_to(base).as_posix())
    if present!=set(names):raise ValueError('Source profile file set differs')
    if {r['path'] for r in language['files']}!=set(EXTRA) or len(language['files'])!=6 or (overlay/'INCOMPLETE').exists():
        raise ValueError('Incomplete English sidecar set')
    output.mkdir(parents=True);(output/'INCOMPLETE').write_text('Bilingual profile seal pending\n',encoding='ascii')
    assets=output/'assets';assets.mkdir();hashes=imp.Hashes();records=[]
    for row in manifest['files']:
        source=base/row['path'];target=assets/row['path'];target.parent.mkdir(parents=True,exist_ok=True)
        if source.stat().st_size!=row['bytes'] or hashes.file(source)!=row['sha256']:
            raise ValueError('Immutable source resource differs: '+row['path'])
        os.link(source,target);records.append(dict(row))
    for row in language['files']:
        source=overlay/imp.relative(row['path']);imp.no_reparse(source)
        if source.stat().st_size!=row['bytes'] or hashes.file(source)!=row['sha256']:
            raise ValueError('English sidecar resource differs')
        target=assets/row['path'];target.parent.mkdir(parents=True,exist_ok=True);os.link(source,target)
        records.append({key:row[key] for key in ('path','bytes','sha256')})
    for name,data in ((BASE,raw),(OVERLAY,overlay_raw)):
        target=assets/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
        records.append(dict(path=name,bytes=len(data),sha256=imp.digest(data)))
    records.sort(key=lambda r:r['path'])
    receipt=dict(format='EDDREAMCAST-LANGUAGES1',base_manifest_path=BASE,base_manifest_sha256=expected_base,
                 overlay_manifest_path=OVERLAY,overlay_manifest_sha256=imp.digest(overlay_raw),
                 policy='Six optional original-English sidecars; every17434 existing file retained byte-exact.')
    result=dict(manifest,files=records,file_count=len(records),files_sha256=imp.digest(imp.json_bytes(records)),
                language_sidecars=receipt,hashed_physical_bytes=hashes.bytes)
    verify_derivation(assets,result)
    if imp.digest(marker.read_bytes())!=expected_base:raise ValueError('Base seal changed during derivation')
    (assets/imp.MARKER).write_bytes(imp.json_bytes(result));(output/'INCOMPLETE').unlink()
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('base','overlay','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--base-sha256',required=True);args=parser.parse_args()
    result=derive(args.base,args.overlay,args.output,args.base_sha256)
    print('Bilingual profile sealed:',result['file_count'],'files')
