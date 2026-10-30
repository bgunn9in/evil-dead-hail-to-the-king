"""Compose a new original Dreamcast asset profile from the recorded generated components.

Relative input paths are relative to this workspace. The raw input directory is
used only for its pinned extraction manifest and unchanged LEVELA/B.JDC copies.
No game/PC resources or original executables are executed. This is an asset import
seal, not a declaration of gameplay completion or original console raster parity.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
from ed_dc_audio import checked_output as checked_original_output

ROOT=Path(__file__).resolve().parents[1]
SOURCE_SHA='cb0c24fee804eb7cc7bc65f4e2311f7498ab1c075f12f7c8cc7f5ff1d4cbc9c9'
FORMAT='ED-DREAMCAST-RUNTIME1'
MARKER='dreamcast-runtime.json'

def digest(data):return hashlib.sha256(data).hexdigest()
def json_bytes(value):return (json.dumps(value,indent=2,ensure_ascii=False)+'\n').encode('utf-8')
def load(path):return json.loads(path.read_text(encoding='utf-8'))
def relative(name):
    if not isinstance(name,str) or not name or '\\' in name or ':' in name or '\0' in name:
        raise ValueError('Invalid relative asset path')
    p=PurePosixPath(name)
    if p.is_absolute() or any(part in ('','.','..') for part in name.split('/')):
        raise ValueError('Asset path traversal is forbidden')
    return p.as_posix()

def no_reparse(path):
    """Check the original spelling and all existing ancestors before resolving."""
    for item in (path,*path.parents):
        if item.is_symlink():raise ValueError('Symlink/reparse input or output: '+str(item))
        try:attributes=getattr(item.lstat(),'st_file_attributes',0)
        except FileNotFoundError:continue
        if attributes&0x400:raise ValueError('Symlink/reparse input or output: '+str(item))

def checked_path(value,exists=True):
    if not isinstance(value,(str,Path)) or not str(value) or '\0' in str(value):raise ValueError('Missing component path')
    path=Path(value)
    if '..' in path.parts:raise ValueError('Parent traversal in component path')
    if not path.is_absolute():path=ROOT/path
    no_reparse(path);path=path.resolve()
    if exists and not path.exists():raise ValueError('Missing generated component: '+str(path))
    return path

def output_guard(output,inputs):
    output=checked_path(output,False)
    checked_original_output(ROOT/'game',output)
    if output.exists():raise FileExistsError('Use a fresh import directory: '+str(output))
    for source in inputs:
        if output==source or output in source.parents or source in output.parents:
            raise ValueError('Output overlaps an immutable input component')
    return output

class Hashes:
    """Read every physical file; repeated hardlinks use a checked stable identity."""
    def __init__(self):self.cache={};self.bytes=0
    def file(self,path):
        no_reparse(path);before=path.stat()
        key=(before.st_dev,before.st_ino,before.st_size,before.st_mtime_ns)
        if key in self.cache:return self.cache[key]
        h=hashlib.sha256()
        with path.open('rb') as stream:
            for block in iter(lambda:stream.read(4*1024*1024),b''):h.update(block);self.bytes+=len(block)
        after=path.stat()
        if (after.st_dev,after.st_ino,after.st_size,after.st_mtime_ns)!=key:raise ValueError('Input changed while hashing: '+str(path))
        self.cache[key]=h.hexdigest();return h.hexdigest()

class Plan:
    def __init__(self):self.files={};self.components=[]
    def add(self,source,destination,*,linked=False,expected=None,overlay=False):
        source=checked_path(source);destination=relative(destination)
        if not source.is_file():raise ValueError('Expected component file: '+str(source))
        if destination in self.files and not overlay:raise ValueError('Duplicate component destination: '+destination)
        if source.suffix.lower() in ('.exe','.dll','.pc','.str','.sfd','.avi','.chd','.iso') or source.name.upper()=='1ST_READ.BIN':
            raise ValueError('Raw executable/media is not an imported runtime component')
        self.files[destination]=dict(source=source,linked=linked,expected=expected)
    def tree(self,source,destination,*,linked=False):
        source=checked_path(source)
        if not source.is_dir():raise ValueError('Expected generated component directory')
        files=[]
        for path in sorted(source.rglob('*')):
            no_reparse(path)
            if path.is_file():files.append(path);self.add(path,(PurePosixPath(destination)/path.relative_to(source).as_posix()).as_posix(),linked=linked)
        if not files:raise ValueError('Empty generated component: '+str(source))
        self.components.append(dict(source=str(source),destination=destination,files=len(files),storage='immutable generated hardlinks' if linked else 'independent copies'))

def catalog(path,magic,count=None):
    rows=path.read_text(encoding='ascii').splitlines()
    if len(rows)<2 or rows[0]!=magic or int(rows[1])!=len(rows)-2 or (count is not None and int(rows[1])!=count):
        raise ValueError('Invalid original runtime catalog: '+str(path))
    return [row.split() for row in rows[2:]]

def plan_inputs(inputs):
    raw=inputs.read_bytes();values=json.loads(raw)
    required={'format','original_files','scenes','actors','objects','audio','streams','movies','ui','debug_inventory_union','russian_tabs','russian_title','vfx'}
    if set(values)!=required or values['format']!='ED-DREAMCAST-RUNTIME-INPUTS1':raise ValueError('Unknown Dreamcast runtime input schema')
    paths={}
    for key in required-{'format'}:
        if key in ('actors','objects','audio','ui'):
            if set(values[key])!={'A','B'}:raise ValueError('Both original level components are required')
            paths[key]={level:checked_path(values[key][level]) for level in 'AB'}
        else:paths[key]=checked_path(values[key])
    all_paths=[inputs]
    for value in paths.values():all_paths.extend(value.values() if isinstance(value,dict) else [value])
    original_manifest=paths['original_files'].parent/'files-manifest.json';no_reparse(original_manifest)
    if digest(original_manifest.read_bytes())!=SOURCE_SHA:raise ValueError('Unknown original extraction identity')
    originals={r['path']:r['sha256'] for r in load(original_manifest)['files']}
    scene_manifest=load(paths['scenes']/'source.json')
    if scene_manifest['format']!='EDDCNATIVESCENES1' or scene_manifest['status']!='PASS' or scene_manifest['source_manifest_sha256']!=SOURCE_SHA or len(scene_manifest['scenes'])!=61:
        raise ValueError('Complete checked61 native scenes are required')
    p=Plan();generated={};script_hashes={}
    streams=load(paths['streams']/'source.json');stream_rows=catalog(paths['streams']/'manifest.txt','EDSTREAM1',52)
    if streams['format']!='EDSTREAM1' or len(streams['clips'])!=52:raise ValueError('Expected52 original Dreamcast STR conversions')
    stream_records={f'{r["id"]:08x}':r for r in streams['clips']}
    movies=load(paths['movies']/'manifest.json')
    if movies['format']!='EDMOVIE1' or len(movies['movies'])!=56:raise ValueError('Expected56 original Dreamcast SFD conversions')
    if len({m['id'] for m in movies['movies']})!=56:raise ValueError('Duplicate original movie IDs')
    vfx=load(paths['vfx']/'source.json')
    if vfx['source_sha256']!=originals['1ST_READ.BIN'] or len(vfx['definitions'])!=19 or len(vfx['files'])!=10:raise ValueError('Complete native VFX definitions/draw contexts required')
    tabs=load(paths['russian_tabs']/'source.json');title=load(paths['russian_title']/'source.json')
    if tabs['format']!='DREAMCAST-RUSSIAN-TABS1' or title['format']!='DREAMCAST-RUSSIAN-TITLE1':raise ValueError('Original-layout Russian tab/title derivations required')
    union=load(paths['debug_inventory_union']/'source.json')
    if union['format']!='EDDREAMCASTUI-DEBUG-UNION1' or union['models']!=96:raise ValueError('Explicit original debug96 union proof required')
    for level in 'AB':
        prefix='' if level=='A' else 'levelB/'
        put=lambda name:prefix+name
        rooms=[r['scene'] for r in scene_manifest['scenes'] if r['level']==level and not r['menu']]
        first='ak01' if level=='A' else 'bm01';rooms=[first]+sorted(set(rooms)-{first})
        generated[put('world.txt')]=('EDWORLD1\n'+first+'\n'+str(len(rooms))+'\n'+'\n'.join(rooms)+'\n').encode('ascii')
        generated[put('episode.json')]=json_bytes(dict(level=level,platform='Dreamcast',scenes=rooms,status='verified-import; gameplay qualification is separate'))
        for record in scene_manifest['scenes']:
            if record['level']==level:p.tree(paths['scenes']/record['scene'],put(record['scene']),linked=True)
        p.tree(paths['actors'][level],put(''));p.tree(paths['objects'][level],put('objects'))
        audio=paths['audio'][level];provenance=load(audio/'dreamcast-audio-provenance.json')
        if provenance['format']!='EDDCAUDIOIMPORT1' or provenance['level']!=level.lower():raise ValueError('Native selected-bank audio provenance required')
        rows=catalog(audio/'audio/manifest.txt','EDAUDIO1');records={(r['bucket'],f'{r["id"]:08x}'):r for r in provenance['records']}
        if len(records)!=len(rows) or digest((audio/'audio/manifest.txt').read_bytes())!=provenance['catalog_sha256']:raise ValueError('Selected native audio catalog/provenance differs')
        p.add(audio/'audio/manifest.txt',put('audio/manifest.txt'),expected=provenance['catalog_sha256'])
        for bucket,identity,size in rows:
            if bucket not in {'global','global_b','aa01','bb01',*rooms} or not re.fullmatch('[0-9a-f]{8}',identity):raise ValueError('Unknown native audio bucket/ID')
            row=records[(bucket,identity)];source=audio/'audio'/bucket/(identity+'.wav')
            if source.stat().st_size!=int(size) or row['wave_bytes']!=int(size):raise ValueError('Native WAV size differs')
            p.add(source,put(f'audio/{bucket}/{identity}.wav'),expected=row['wave_sha256'])
        p.add(audio/'door.wav',put('door.wav'),expected=provenance['pinned_door']['wave_sha256'])
        p.add(audio/'dreamcast-audio-provenance.json',put('dreamcast-audio-provenance.json'))
        # Select only catalogued original streams, never the mixed fixture's audio tree.
        for identity,size in stream_rows:
            if not re.fullmatch('[0-9a-f]{8}',identity):raise ValueError('Invalid native stream ID')
            row=stream_records[identity]
            if not row['source'].endswith('.STR') or originals.get(row['source'])!=row['source_sha256']:raise ValueError('Stream belongs to another original source')
            file=paths['streams']/(identity+'.wav')
            if file.stat().st_size!=int(size) or row['bytes']!=int(size):raise ValueError('Native stream size differs')
            p.add(file,put('audio/streams/'+file.name),linked=True,expected=row['sha256'])
        for name in ('manifest.txt','source.json'):p.add(paths['streams']/name,put('audio/streams/'+name),linked=True)
        for row in movies['movies']:
            if not row['source_name'].endswith('.SFD') or originals.get(row['source_name'])!=row['source_sha256']:raise ValueError('Movie belongs to another original source')
            stem=f'{row["id"]:08x}';file=paths['movies']/(stem+'.edm')
            if file.stat().st_size!=row['decoded_bytes']:raise ValueError('Original decoded movie size differs')
            p.add(file,put('movies/'+file.name),linked=True,expected=row['decoded_sha256'])
            p.add(paths['movies']/(stem+'.json'),put('movies/'+stem+'.json'),linked=True)
        p.add(paths['movies']/'manifest.json',put('movies/manifest.json'),linked=True)
        script=paths['original_files']/f'LEVEL{level}.JDC';script_hashes[level]=originals[script.name]
        p.add(script,put(f'scripts/level{level}.jpc'),expected=originals[script.name])
        own=load(paths['ui'][level]/'gui/source.json')
        if own['models']!=(95 if level=='A' else 102):raise ValueError('Original own-package GUI catalog differs')
        p.tree(paths['ui'][level],put(''))
        if level=='A':
            for file in sorted(paths['debug_inventory_union'].rglob('*')):
                no_reparse(file)
                if file.is_file():
                    name=file.relative_to(paths['debug_inventory_union']).as_posix()
                    if name not in {'source.json','gui/layout.bin','gui/uv-ordinals.bin','gui/all-models/0b142248.mesh','gui/textures/0b942248.rgba'}:raise ValueError('Unexpected debug union overlay')
                    p.add(file,put('debug-inventory-union.json' if name=='source.json' else name),overlay=True)
        p.add(paths['russian_tabs']/'gui/textures/0b942054.rgba',put('gui/textures/0b942054.rgba'),expected=tabs['encoded_sha256'],overlay=True)
        p.add(paths['russian_tabs']/'source.json',put('gui/russian-tabs.json'))
        menu='aa01' if level=='A' else 'bb01'
        for name in ('gui/textures/0b942034.rgba',menu+'/main-menu.texture'):
            p.add(paths['russian_title']/level/name,put(name),expected=title['encoded_sha256'],overlay=True)
        p.add(paths['russian_title']/'source.json',put('gui/russian-title.json'))
        for name,record in vfx['files'].items():p.add(paths['vfx']/relative(name),put('vfx/'+name),expected=record['sha256'])
        p.add(paths['vfx']/'source.json',put('vfx/source.json'))
    generated['provenance/import-inputs.json']=raw
    return p,generated,all_paths,dict(source_manifest_sha256=SOURCE_SHA,original_script_sha256=script_hashes,unsupported=scene_manifest['unsupported'],
        gui_policy=dict(A_own=95,A_debug_union=96,B_own=102,Russian_derived_textures=['0b942054','0b942034']),
        scope='Verified original asset import;19 VFX kinds via bounded host bridge. This status does not mean gameplay completion or original console timeline/raster parity.')

def compose(inputs,output):
    inputs=checked_path(inputs);output=output_guard(output,[inputs])
    plan,generated,sources,metadata=plan_inputs(inputs);output=output_guard(output,sources)
    if set(plan.files)&set(generated):raise ValueError('Generated metadata collides with component')
    output.mkdir(parents=True);(output/'INCOMPLETE').write_text('Original asset copy/hash seal in progress; do not select or package.\n',encoding='ascii')
    assets=output/'assets';assets.mkdir();hashes=Hashes();expected={}
    try:
        for index,(name,item) in enumerate(plan.files.items()):
            source=item['source'];target=assets/relative(name);target.parent.mkdir(parents=True,exist_ok=True)
            if item['expected'] is None:item['expected']=hashes.file(source)
            if item['linked']:os.link(source,target)
            else:shutil.copyfile(source,target)
            expected[name]=item['expected']
            if index%1000==0:print(f'Imported {index+1}/{len(plan.files)} component files',flush=True)
        for name,data in generated.items():
            target=assets/relative(name);target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data);expected[name]=digest(data)
        records=[]
        for index,(name,value) in enumerate(sorted(expected.items())):
            target=assets/name;actual=hashes.file(target)
            if actual!=value:raise ValueError('Copied output hash differs: '+name)
            records.append(dict(path=name,bytes=target.stat().st_size,sha256=actual))
            if index%1000==0:print(f'Hash sealed {index+1}/{len(expected)} asset files',flush=True)
        result=dict(format=FORMAT,status='verified-import',**metadata,inputs_manifest_sha256=digest(inputs.read_bytes()),
            components=plan.components,files=records,file_count=len(records),files_sha256=digest(json_bytes(records)),
            seal_scope='All assets except dreamcast-runtime.json itself. Copied files independently reread; immutable generated hardlinks use stable file identity and full physical-file SHA256.',hashed_physical_bytes=hashes.bytes)
        pending=assets/(MARKER+'.pending');pending.write_bytes(json_bytes(result));os.replace(pending,assets/MARKER)
        (output/'INCOMPLETE').unlink();return result
    except Exception as error:
        (output/'import-error.json').write_bytes(json_bytes(dict(status='INCOMPLETE',error=str(error))))
        raise

def verify(inputs,output):
    inputs,output=checked_path(inputs),checked_path(output,False)
    checked_original_output(ROOT/'game',output)
    values=load(inputs)
    if values.get('format')=='ED-DREAMCAST-RUNTIME-INPUTS1':
        source=checked_path(values['original_files'],False)
        checked_original_output(source,output)
        if output in source.parents:raise ValueError('Verification root overlaps original source files')
    if not output.exists():raise ValueError('Missing imported profile')
    if (output/'INCOMPLETE').exists():raise ValueError('Import is incomplete')
    assets=output/'assets';manifest=load(assets/MARKER)
    if manifest.get('format')!=FORMAT or manifest.get('status')!='verified-import' or manifest['inputs_manifest_sha256']!=digest(inputs.read_bytes()):raise ValueError('Unknown import seal/input identity')
    records=manifest['files']
    if len(records)!=manifest['file_count'] or digest(json_bytes(records))!=manifest['files_sha256']:raise ValueError('Import seal changed')
    visual_manifest=manifest
    if 'lossless_storage' in visual_manifest:
        from compress_dreamcast_runtime import verify_derivation as verify_lossless
        visual_manifest=verify_lossless(assets,visual_manifest)
    if 'ui_revision' in visual_manifest:
        from derive_dreamcast_ui import verify_derivation as verify_ui
        visual_manifest=verify_ui(assets,visual_manifest)
    if 'language_sidecars' in visual_manifest:
        from derive_dreamcast_language import verify_derivation as verify_language
        visual_manifest=verify_language(assets,visual_manifest)
    if 'visual_upscale' in visual_manifest:
        from upscale_dreamcast_runtime import verify_derivation
        verify_derivation(assets,visual_manifest)
    names=[relative(r['path']) for r in records]
    if len(set(names))!=len(names):raise ValueError('Duplicate sealed asset paths')
    present=set()
    for path in assets.rglob('*'):
        no_reparse(path)
        if path.is_file() and path!=assets/MARKER:present.add(path.relative_to(assets).as_posix())
    if present!=set(names):raise ValueError('Imported asset file set differs')
    hashes=Hashes()
    for index,row in enumerate(records):
        file=assets/row['path']
        if file.stat().st_size!=row['bytes'] or hashes.file(file)!=row['sha256']:raise ValueError('Imported asset bytes differ: '+row['path'])
        if index%1000==0:print(f'Verified {index+1}/{len(records)} sealed asset files',flush=True)
    return dict(format=FORMAT,status='verified-import',file_count=len(records),rewritten_files=0,manifest_sha256=digest((assets/MARKER).read_bytes()),hashed_physical_bytes=hashes.bytes)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--inputs',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--verify',action='store_true',help='Read-only verification of a completed assembled profile against every sealed file.')
    args=parser.parse_args();result=(verify if args.verify else compose)(args.inputs,args.output)
    print(json.dumps({k:result[k] for k in ('format','status','file_count')},ensure_ascii=False))

if __name__=='__main__':main()
