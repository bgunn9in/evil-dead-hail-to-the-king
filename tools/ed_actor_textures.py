"""Export verified original default/variant actor textures without altering actor meshes."""
import argparse
import hashlib
import json
from pathlib import Path

from ed_assets import parse_archive, parse_dc_archive
from ed_actions import EXE_SHA256
from ed_model import decode_texture
from ed_objects import encode_texture
from ed_scripts import ScriptPackage, _successors

ROOT=Path(__file__).resolve().parents[1]
# Source ActorDB.createType657 -> create643 supplies these actor families; these
# labels do not merge distinct resource IDs or strip their low variant bits.
VARIANT_ACTORS={0x080C6004:(4,"Deadite"),0x083CC004:(13,"DemonPig"),0x081CA004:(11,"HellbillyFat"),
                0x081D8004:(12,"HellbillyThin"),0x08318004:(7,"TroopKnife"),
                0x08198004:(23,"GuildMember"),0x08198008:(23,"GuildMember")}


def canonical(identity):
    if not isinstance(identity,int) or not 0<=identity<=0xffffffff:
        raise ValueError("Actor texture identity is outside uint32")
    return identity&0x1fffffff  # Native4038C0 ->4190C0; preserves all low variant bits.


def audit_callers(game, *, dreamcast=False):
    packages=[]
    for name in ("levelA","levelB"):
        path=Path(game)/(f"{name.upper()}.JDC" if dreamcast else f"data/script/pc/{name}.jpc");data=path.read_bytes();package=ScriptPackage(data)
        target=next(m["index"] for m in package.methods if (m["class_name"],m["name"],m["descriptor"])==("ed/base/EDActor","setTexture","(I)V"))
        external=next(m["index"] for m in package.methods if (m["class_name"],m["name"],m["descriptor"])==("ed/base/External","actorSetTexture","(III)V"))
        calls=[];native_calls=[]
        for method in package.methods:
            code=package.disassemble(method["index"])
            switches=[i for i in code if i["mnemonic"] in ("tableswitch","lookupswitch")]
            is_flavor=method["class_name"]=="GenericFlavors" and method["name"]=="setFlavor"
            successors=_successors(code) if is_flavor else {}
            for index,instruction in enumerate(code):
                called=instruction.get("method",{}).get("index")
                if called==external:native_calls.append({"method":method["index"],"owner":method["class_name"],"name":method["name"],"pc":instruction["offset"]})
                if called!=target:continue
                previous=code[index-1] if index else {}
                if previous.get("opcode") not in (0x12,0x13) or previous["constant"]["index_raw"]<0:
                    raise ValueError("Actor setTexture argument is not a verified immediate resource ID")
                identity=canonical(previous["constant"]["value_raw"])
                flavors=[]
                if is_flavor:
                    for flavor,start in switches[0]["cases"]:
                        pending=[start];seen=set()
                        while pending:
                            offset=pending.pop()
                            if offset in seen:continue
                            seen.add(offset)
                            if offset==instruction["offset"]:flavors.append(flavor);break
                            pending.extend(successors[offset])
                calls.append({"method":method["index"],"owner":method["class_name"],"name":method["name"],
                              "pc":instruction["offset"],"argument_pc":previous["offset"],"resource_id":identity,
                              "flavors":flavors,"default_reset":method["class_name"]=="ActorDB" and method["name"]=="reinitialize"})
        packages.append({"path":path.relative_to(game).as_posix(),"sha256":hashlib.sha256(data).hexdigest(),
                         "set_texture_method":target,"native_method":external,"callers":calls,"native_callers":native_calls})
    return packages


def read_actor_textures(game,default_ids=None, *, dreamcast=False):
    game=Path(game).resolve()
    from ed_dc_actions import EXECUTABLE_SHA256
    executable = game / ("1ST_READ.BIN" if dreamcast else "EvilDead.exe")
    if hashlib.sha256(executable.read_bytes()).hexdigest()!=(EXECUTABLE_SHA256 if dreamcast else EXE_SHA256):
        raise ValueError("Actor texture variants require the verified original PC executable")
    if default_ids is None:
        from export_actor import ACTORS
        defaults={name:canonical(value["texture"]) for name,value in ACTORS.items()}
    elif hasattr(default_ids,"items"):
        defaults={str(name):canonical(value) for name,value in default_ids.items()}
    else:defaults={f"default_{canonical(value):08x}":canonical(value) for value in default_ids}
    packages=audit_callers(game,dreamcast=dreamcast)
    variants={c["resource_id"] for p in packages for c in p["callers"] if not c["default_reset"]}
    if variants!=set(VARIANT_ACTORS):raise ValueError("Original setTexture variant list changed; audit required")
    wanted=set(defaults.values())|variants
    if not wanted or any(identity>>24!=8 for identity in wanted):raise ValueError("Expected original type08 texture IDs")
    records={identity:{"resource_id":identity,"sources":[],"rejected_sources":[],"archetypes":[name for name,value in defaults.items() if value==identity],"variant":identity in variants} for identity in wanted}
    for identity in variants:
        actor_type,actor_class=VARIANT_ACTORS[identity];records[identity].update(actor_type=actor_type,actor_class=actor_class)
        records[identity]["archetypes"].append(actor_class.lower())
    checked=0
    paths = game.glob("*.DC") if dreamcast else (game/"data").glob("*.pc")
    for path in sorted(paths):
        data=path.read_bytes();entries=(parse_dc_archive if dreamcast else parse_archive)(data);checked+=1
        matches=[e for e in entries if e.resource_id in wanted]
        if not matches:continue
        archive_hash=hashlib.sha256(data).hexdigest()
        for entry in matches:
            payload=data[entry.offset:entry.offset+entry.size];digest=hashlib.sha256(payload).hexdigest();record=records[entry.resource_id]
            source={"archive":path.name,"archive_sha256":archive_hash,"entry":entry.index,"flags":entry.flags,"offset":entry.offset,"size":entry.size}
            try:
                if entry.flags!=1:raise ValueError("Original actor DDS textures require resource flags1")
                if dreamcast:
                    from ed_dc_model import decode_texture as dc_texture
                    texture,name=dc_texture(payload,entry.resource_id)
                else:
                    texture,name=decode_texture(payload)
            except (ValueError,OSError) as error:
                record["rejected_sources"].append({**source,"payload_sha256":digest,"reason":str(error)});continue
            if record["sources"] and digest!=record["payload_sha256"]:raise ValueError(f"Conflicting original actor texture {entry.resource_id:08x}")
            if not record["sources"]:
                record.update(payload_sha256=digest,source_name=name,width=texture.width,height=texture.height,
                              rgba_sha256=hashlib.sha256(texture.tobytes()).hexdigest(),texture=texture)
            record["sources"].append(source)
    missing=[f"{identity:08x}" for identity,record in records.items() if not record["sources"]]
    if missing:raise ValueError("Missing exact original actor DDS texture: "+", ".join(missing))
    return {"packages":packages,"archive_count":checked,"textures":[records[i] for i in sorted(records)]}


def export_actor_textures(game,output,default_ids=None, *, dreamcast=False):
    game,output=Path(game).resolve(),Path(output).resolve()
    if output==game or game in output.parents:raise ValueError("Actor textures must be exported outside original game directory")
    assets=read_actor_textures(game,default_ids,dreamcast=dreamcast);target=output/"actors/textures"
    if output not in target.resolve().parents:raise ValueError("Actor texture output escapes generated assets")
    target.mkdir(parents=True,exist_ok=True);lines=["EDACTORTEX1",str(len(assets["textures"]))]
    for texture in assets["textures"]:
        identity=texture["resource_id"];path=target/f"{identity:08x}.rgba"
        if output not in path.resolve().parents:raise ValueError("Actor texture output escapes generated assets")
        encoded=encode_texture(identity,texture["texture"]);path.write_bytes(encoded);lines.append(f"{identity:08x}")
        texture["encoded_bytes"]=len(encoded);texture["encoded_sha256"]=hashlib.sha256(encoded).hexdigest()
    metadata={"format":"EDACTORTEX1","native_identity_mask":"0x1fffffff; upper3bits only; low variant bits remain exact",
              "executable_sha256":hashlib.sha256((game/("1ST_READ.BIN" if dreamcast else "EvilDead.exe")).read_bytes()).hexdigest(),
              "archive_count":assets["archive_count"],"packages":assets["packages"],
              "textures":[{k:v for k,v in t.items() if k!="texture"} for t in assets["textures"]]}
    for name,data in (("manifest.txt",("\n".join(lines)+"\n").encode("ascii")),("source.json",(json.dumps(metadata,indent=2)+"\n").encode("utf-8"))):
        path=target/name
        if output not in path.resolve().parents:raise ValueError("Actor texture metadata escapes generated assets")
        path.write_bytes(data)
    return metadata


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--game",type=Path,default=ROOT/"game")
    parser.add_argument("--output",type=Path,default=ROOT/"build/episode/assets");args=parser.parse_args()
    result=export_actor_textures(args.game,args.output);print(json.dumps({"textures":len(result["textures"]),"output":str(args.output/"actors/textures")}))


if __name__=="__main__":main()
