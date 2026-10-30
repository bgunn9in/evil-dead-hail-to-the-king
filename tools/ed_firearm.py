"""Read-only audit of every original Ash36-byte animation event and firearm FSM."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

from ed_actions import EXE_SHA256,parse_ash_actions
from ed_assets import parse_archive
from ed_model import decode_animations
from ed_scripts import ScriptPackage


def audit_firearms(game):
    game=Path(game);exe=(game/"EvilDead.exe").read_bytes()
    if hashlib.sha256(exe).hexdigest()!=EXE_SHA256:raise ValueError("Unverified original PC executable")
    archive=(game/"data/az01.pc").read_bytes();states=parse_ash_actions(exe,archive)
    counts=Counter();records=[];packs=[]
    for entry in parse_archive(archive):
        if entry.resource_id not in (0x01024000,0x01824184):continue
        payload=archive[entry.offset:entry.offset+entry.size];clips=decode_animations(payload)
        starts=struct.unpack_from(f"<{len(clips)}I",payload,4)
        packs.append({"id":entry.resource_id,"archive_offset":entry.offset,"size":entry.size,"sha256":hashlib.sha256(payload).hexdigest()})
        for clip,start in zip(clips,starts):
            at=entry.offset+start+40+clip["frame_count"]*(3+clip["bone_count"]*3)*4
            for n,hexraw in enumerate(clip["events36_raw"]):
                raw=bytes.fromhex(hexraw);frame,kind,enabled,mode=struct.unpack_from("<HBBi",raw)
                counts[kind]+=1
                if kind not in (7,8):continue
                records.append({"clip":clip["resource_id"],"frames":clip["frame_count"],"frame":frame,"kind":kind,
                    "enabled":enabled,"mode":mode,"first":list(struct.unpack_from("<3f",raw,8)),
                    "second":list(struct.unpack_from("<3f",raw,20)),"trailing":struct.unpack_from("<I",raw,32)[0],
                    "raw":hexraw,"archive_offset":at+n*36,
                    "states":[s["index"]for s in states if s["animation_id"]==clip["resource_id"]]})
    source=game/"data/script/pc/levelA.jpc";package=ScriptPackage(source.read_bytes())
    methods=[]
    for index in (659,673,682,711,718,1008,1009,1011,1012,1020,1021,1024,1026,1035,1043,
                  1044,1045,1047,1048,1050,1051,1053,1292,1309,649,1580,1597):
        m=package.methods[index];methods.append({"index":index,"owner":m["class_name"],"name":m["name"],
            "descriptor":m["descriptor"],"code_offset":m["code_offset"],"code_length":m["code_length"],
            "sha256":hashlib.sha256(package.data[56+m["code_offset"]:56+m["code_offset"]+m["code_length"]]).hexdigest()})
    return {"format":"EDPLAYERFIREARM1","source":{"exe_sha256":EXE_SHA256,
        "archive":"game/data/az01.pc","archive_sha256":hashlib.sha256(archive).hexdigest(),
        "jpc":"game/data/script/pc/levelA.jpc","jpc_sha256":hashlib.sha256(source.read_bytes()).hexdigest()},
        "packs":packs,"all_raw36_counts":dict(sorted(counts.items())),"firearm_records":records,"methods":methods}


if __name__=="__main__":
    cli=argparse.ArgumentParser(description=__doc__);cli.add_argument("--game",type=Path,default=Path("game"));cli.add_argument("--output",type=Path)
    args=cli.parse_args();text=json.dumps(audit_firearms(args.game),indent=2)+"\n"
    if args.output:
        if args.output.resolve().is_relative_to(args.game.resolve()):raise ValueError("Original game is read-only")
        args.output.write_text(text,encoding="utf-8")
    else:print(text,end="")
