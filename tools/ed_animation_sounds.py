"""Audit original reachable actor sound records and the native Ash SFX callback."""
import hashlib
import json
from pathlib import Path
import re
import struct

from ed_actions import parse_actor_actions
from ed_assets import parse_archive
from ed_audio import read_audio_assets
from ed_model import decode_animations
from ed_scripts import ScriptPackage
from export_actor import ACTORS,LEVEL_B_ACTORS

TYPES={"ash":0,"skullbat":2,"deadite":4,"treeboss":5,"skeleton":6,"troopknife":7,"troopclaw":8,
       "trooprange":9,"troopleaderboss":10,"hellbillyfat":11,"hellbillythin":12,"demonpig":13,
       "matriarchboss":14,"deadash":17,"anniezombie":18,"treearm":34,"headlessskeleton":35}


def audit_animation_sounds(game,level="A"):
    if level not in("A","B"):raise ValueError("Expected original package A or B")
    configs=ACTORS if level=="A"else LEVEL_B_ACTORS;global_bucket="global"if level=="A"else"global_b"
    game=Path(game);exe=(game/"EvilDead.exe").read_bytes();archives={}
    scopes=sorted({c["archive"][:-3]for c in configs.values()}-{"az01","bz01"})
    bank=read_audio_assets(game,scopes);clips={(c["bucket"],c["id"]):c for c in bank["clips"]};actors=[]
    for name,config in configs.items():
        blob=archives.setdefault(config["archive"],(game/"data"/config["archive"]).read_bytes())
        states=parse_actor_actions(exe,blob,config.get("action_actor",name));used={s["animation_id"]for s in states};records=[];inactive=0;count=0
        for entry in parse_archive(blob):
            if entry.resource_id not in config["packs"]:continue
            payload=blob[entry.offset:entry.offset+entry.size];animations=decode_animations(payload)
            starts=struct.unpack_from(f"<{len(animations)}I",payload,4)
            for clip,start in zip(animations,starts):
                if clip["resource_id"]not in used:continue
                count+=1;offset=entry.offset+start+40+clip["frame_count"]*(3+3*clip["bone_count"])*4+36*len(clip["events36_raw"])
                for index,rawhex in enumerate(clip["events20_raw"]):
                    raw=bytes.fromhex(rawhex);identity=struct.unpack_from("<I",raw)[0]
                    active=bool(raw[11]and raw[8]<clip["frame_count"])
                    resolved=clips.get((config["archive"][:-3],identity),clips.get((global_bucket,identity)))
                    if active and resolved is None:raise ValueError(f"Unresolved original animation sound {name}:{identity:08x}")
                    if not active:inactive+=1
                    records.append({"clip":clip["resource_id"],"frame_count":clip["frame_count"],"record_index":index,
                        "archive_offset":offset+20*index,"raw":rawhex,"resource_id":identity,"frame":raw[8],
                        "authored_volume":raw[9],"initial_hz":(raw[10] or 22)*1000,"active":active,
                        "resolved_bucket":resolved["bucket"]if resolved else None,
                        "wave_sha256":resolved["sha256"]if resolved else None})
        actors.append({"name":name,"native_type":TYPES[name]if level=="A"else config["native_type"],"archive":config["archive"],
            "archive_sha256":hashlib.sha256(blob).hexdigest(),"reachable_clips":count,
            "records":records,"inactive_records":inactive})
    methods=[]
    for package_level in("A","B"):
        data=(game/f"data/script/pc/level{package_level}.jpc").read_bytes();package=ScriptPackage(data);selected=[]
        for method in package.methods:
            if(method["class_name"],method["name"])not in{("ActorDB","processSFXAction"),("Ash","processSFXAction"),
                ("Ash","setSoundFootStep"),("Ash","clearSoundFootSteps")}:continue
            start=56+method["code_offset"];record={**method,"file_offset":start,
                "code_sha256":hashlib.sha256(data[start:start+method["code_length"]]).hexdigest()}
            if method["class_name"]=="Ash"and method["name"]=="processSFXAction":
                switch=next(i for i in package.disassemble(method["index"])if i["mnemonic"]=="tableswitch")
                record["resource_cases"]=switch["cases"];record["default_pc"]=switch["default"]
            selected.append(record)
        methods.append({"level":package_level,"sha256":hashlib.sha256(data).hexdigest(),"methods":selected})
    return {"level":level,"global_bucket":global_bucket,"executable_sha256":hashlib.sha256(exe).hexdigest(),"actors":actors,"scripts":methods}


def verify_audio_manifest(game,output,audit=None):
    """Check exact IDs/PCM for archetype scopes and source-declared episode spawns."""
    output=Path(output);audit=audit or audit_animation_sounds(game);level=audit.get("level","A");global_bucket="global"if level=="A"else"global_b";path=output/"audio/manifest.txt"
    if path.stat().st_size>1024*1024:raise ValueError("Audio manifest is too large")
    words=path.read_text(encoding="ascii").split()
    if len(words)<2 or words[0]!="EDAUDIO1":raise ValueError("Invalid audio manifest")
    count=int(words[1])
    if not 0<count<=4096 or len(words)!=2+count*3:raise ValueError("Invalid audio manifest count")
    exported={}
    for i in range(count):
        bucket,hexid,length=words[2+i*3:5+i*3];length=int(length)
        if not re.fullmatch(r"global(_b)?|[a-z]{2}[0-9]{2}",bucket)or not re.fullmatch(r"[0-9a-f]{8}",hexid)or not 44<=length<=16*1024*1024:
            raise ValueError("Invalid audio manifest entry")
        key=bucket,int(hexid,16)
        if key in exported:raise ValueError("Duplicate audio manifest entry")
        file=output/"audio"/bucket/(hexid+".wav")
        if file.stat().st_size!=length:raise ValueError("Exported audio length differs from manifest")
        exported[key]=hashlib.sha256(file.read_bytes()).hexdigest()
    scopes=sorted({bucket for bucket,_ in exported if bucket not in("global","global_b")})
    original=read_audio_assets(game,scopes);source={(c["bucket"],c["id"]):c["sha256"]for c in original["clips"]}
    actors={a["native_type"]:a for a in audit["actors"]};checks=set()
    for actor in actors.values():
        scope=actor["archive"][:-3]
        if scope in("az01","bz01"):scope=global_bucket
        if scope in scopes or scope==global_bucket:checks.add((scope,actor["native_type"]))
    coverage=output/("levela-coverage.json"if level=="A"else"levelb-coverage.json")
    if coverage.exists():
        data=json.loads(coverage.read_text(encoding="utf-8"))
        if data["source_script_sha256"]!=audit["scripts"][0 if level=="A"else 1]["sha256"]:raise ValueError("Audio scene coverage refers to another JPC")
        for scene in data["scenes"]:
            if scene["scene"]not in scopes:continue
            types=(set(scene["actor_prototypes"])if level=="A"else{call["native_type"]for call in scene["spawn_calls"]})|{0}
            if 5 in types:types.add(34) # Original TreeBoss owns its linked arm.
            checks.update((scene["scene"],t)for t in types if t in actors)
    missing=[];verified=0
    for scene,actor_type in sorted(checks):
        for identity in {r["resource_id"]for r in actors[actor_type]["records"]if r["active"]}:
            key=(scene,identity)if(scene,identity)in source else(global_bucket,identity)
            if key not in source:missing.append({"scene":scene,"actor_type":actor_type,"id":identity,"reason":"no original PCM in current scene/global"})
            elif exported.get(key)!=source[key]:missing.append({"scene":scene,"actor_type":actor_type,"id":identity,"reason":"missing or mismatched exported PCM"})
            else:verified+=1
    return {"checked_actor_scene_pairs":len(checks),"checked_exact_ids":verified,"missing":missing}
