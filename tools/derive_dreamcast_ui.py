"""Create a fresh font/Russian-text revision without changing sealed source profiles."""
import argparse
import os
from pathlib import Path

import import_dreamcast_runtime as imp
from derive_dreamcast_language import inventory_shape

FORMAT='EDDREAMCAST-UI-REVISION1'
BASE='provenance/ui-base.json'
OVERLAY='provenance/ui-overlay.json'
ALLOWED={prefix+name for prefix in ('','levelB/') for name in (
    'font.bmp','gui/text-ru.bin','inventory.bin','gui/textures/0b942034.rgba',
    'gui/textures/0b942054.rgba','gui/russian-title.json','gui/russian-tabs.json')}
ALLOWED.update(('aa01/main-menu.texture','levelB/bb01/main-menu.texture'))


def validate_tables(manifest,base,overlay):
    if (base.get('format')!=imp.FORMAT or base.get('status')!='verified-import' or 'ui_revision' in base or
            base.get('source_manifest_sha256')!=imp.SOURCE_SHA or
            base['inputs_manifest_sha256']!=manifest['inputs_manifest_sha256'] or
            imp.digest(imp.json_bytes(base['files']))!=base['files_sha256']):
        raise ValueError('Invalid immutable UI base')
    if overlay.get('format')!=FORMAT or not overlay.get('files'):
        raise ValueError('Invalid UI overlay')
    before={r['path']:r for r in base['files']};after={r['path']:r for r in manifest['files']}
    changes={r['path']:r for r in overlay['files']}
    for name in set(before)|set(after)|set(changes):imp.relative(name)
    if (len(before)!=len(base['files']) or len(after)!=len(manifest['files']) or
            len(changes)!=len(overlay['files']) or set(after)!=set(before)|{BASE,OVERLAY} or
            not set(changes)<=ALLOWED or not set(changes)<=set(before)):
        raise ValueError('UI revision changed resource identities')
    for name,row in before.items():
        if name not in changes:
            if after[name]!=row:raise ValueError('Non-UI resource changed: '+name)
        else:
            change=changes[name]
            if change['source_sha256']!=row['sha256'] or after[name]!={k:change[k]for k in ('path','bytes','sha256')} or after[name]==row:
                raise ValueError('UI change has no exact before/after identity')
    return changes


def verify_derivation(assets,manifest):
    receipt=manifest['ui_revision']
    if receipt.get('format')!=FORMAT or receipt.get('base_manifest_path')!=BASE or receipt.get('overlay_manifest_path')!=OVERLAY:
        raise ValueError('Unknown UI revision')
    imp.no_reparse(assets/BASE);imp.no_reparse(assets/OVERLAY)
    raw=(assets/BASE).read_bytes();overlay_raw=(assets/OVERLAY).read_bytes()
    if imp.digest(raw)!=receipt['base_manifest_sha256'] or imp.digest(overlay_raw)!=receipt['overlay_manifest_sha256']:
        raise ValueError('UI revision provenance changed')
    base=imp.load(assets/BASE);overlay=imp.load(assets/OVERLAY)
    changes=validate_tables(manifest,base,overlay)
    for name,row in changes.items():
        imp.no_reparse(assets/name)
        payload=(assets/name).read_bytes()
        if len(payload)!=row['bytes'] or imp.digest(payload)!=row['sha256']:
            raise ValueError('UI overlay bytes differ: '+name)
        if name.endswith('inventory.bin'):
            english=assets/('levelB/' if name.startswith('levelB/')else '')/'gui/languages/en/inventory.bin'
            imp.no_reparse(english)
            if inventory_shape(payload)!=inventory_shape(english.read_bytes(),True):
                raise ValueError('Translated inventory changed IDs/geometry')
    return base


def derive(base,overlay,output,expected_base):
    base,overlay=imp.checked_path(base),imp.checked_path(overlay)
    output=imp.output_guard(output,(base,overlay))
    if (overlay/'INCOMPLETE').exists():raise ValueError('UI overlay is incomplete')
    marker=base/imp.MARKER;imp.no_reparse(marker);raw=marker.read_bytes();source=imp.load(marker)
    if imp.digest(raw)!=expected_base or source.get('status')!='verified-import' or 'ui_revision'in source:
        raise ValueError('Unexpected immutable UI source profile')
    overlay_marker=overlay/'manifest.json';imp.no_reparse(overlay_marker)
    overlay_raw=overlay_marker.read_bytes();revision=imp.load(overlay_marker)
    before={r['path']:r for r in source['files']};changed={r['path']:r for r in revision['files']}
    rows={name:{k:r[k]for k in ('path','bytes','sha256')}for name,r in changed.items()}
    records={**before,**rows,BASE:dict(path=BASE,bytes=len(raw),sha256=imp.digest(raw)),
             OVERLAY:dict(path=OVERLAY,bytes=len(overlay_raw),sha256=imp.digest(overlay_raw))}
    manifest=dict(source,files=[records[n]for n in sorted(records)],file_count=len(records),
                  ui_revision=dict(format=FORMAT,base_manifest_path=BASE,base_manifest_sha256=expected_base,
                                   overlay_manifest_path=OVERLAY,overlay_manifest_sha256=imp.digest(overlay_raw)))
    manifest['files_sha256']=imp.digest(imp.json_bytes(manifest['files']))
    validate_tables(manifest,source,revision)
    present=set()
    for path in base.rglob('*'):
        imp.no_reparse(path)
        if path.is_file()and path!=marker:present.add(path.relative_to(base).as_posix())
    if present!=set(before):raise ValueError('Immutable base file set changed')
    output.mkdir(parents=True);(output/'INCOMPLETE').write_text('UI revision verification pending\n',encoding='ascii')
    assets=output/'assets';assets.mkdir();hashes=imp.Hashes()
    for name,row in before.items():
        original=base/name
        if original.stat().st_size!=row['bytes']or hashes.file(original)!=row['sha256']:
            raise ValueError('Immutable base resource differs: '+name)
        source_file=overlay/name if name in changed else original
        imp.no_reparse(source_file);expected=records[name]
        if source_file.stat().st_size!=expected['bytes']or hashes.file(source_file)!=expected['sha256']:
            raise ValueError('Invalid new UI resource: '+name)
        target=assets/name;target.parent.mkdir(parents=True,exist_ok=True);os.link(source_file,target)
    for name,payload in ((BASE,raw),(OVERLAY,overlay_raw)):
        target=assets/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(payload)
    verify_derivation(assets,manifest)
    if imp.digest(marker.read_bytes())!=expected_base:raise ValueError('Base seal changed during UI revision')
    (assets/imp.MARKER).write_bytes(imp.json_bytes(manifest))
    (output/'INCOMPLETE').unlink()
    try:imp.verify(assets/'provenance/import-inputs.json',output)
    except Exception:
        (output/'INCOMPLETE').write_text('UI revision verification failed\n',encoding='ascii');raise
    return manifest


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('base','overlay','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--base-sha256',required=True);args=parser.parse_args()
    result=derive(args.base,args.overlay,args.output,args.base_sha256)
    print('UI revision sealed:',result['file_count'],'files')
