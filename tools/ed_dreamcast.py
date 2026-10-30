"""Inventory extracted original Dreamcast resources without replacing assets."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

from ed_assets import parse_dc_archive


def classify(identity,flags):
    kind=identity>>24
    if flags==0 and kind&31==17:return "text"
    if flags==0 and kind==15:return "scene"
    if flags==0 and kind==11:return "gui-model"
    if flags==0 and kind==8:return "background-index-mask"
    if flags==1 and kind in (8,11):return "texture"
    if flags==1 and kind==13:return "animation-sound-bank"
    if flags==1 and kind==14:return "script-sound-bank"
    if flags==0 and kind<8:return "actor-resource"
    return "unknown"


def inventory(source):
    source=Path(source).resolve()
    paths=sorted(p for p in source.iterdir() if p.is_file() and p.suffix.upper()==".DC")
    if not paths:raise ValueError("No extracted original .DC archives")
    archives=[];counts=Counter()
    for path in paths:
        raw=path.read_bytes();entries=parse_dc_archive(raw);records=[]
        for entry in entries:
            payload=raw[entry.offset:entry.offset+entry.size]
            category=classify(entry.resource_id,entry.flags);counts[category]+=1
            record={"index":entry.index,"id":f"{entry.resource_id:08x}","flags":entry.flags,
                    "offset":entry.offset,"bytes":entry.size,"sha256":hashlib.sha256(payload).hexdigest(),"kind":category}
            if category=="text":
                end=payload.find(b"\0")
                if end<0 or any(payload[end:]):raise ValueError(f"Unverified DC text padding: {path.name}/{entry.index}")
                record["text_bytes"]=end
                record["ascii"]=all(32<=c<127 or c in (9,10,13) for c in payload[:end])
            records.append(record)
        archives.append({"path":path.name,"bytes":len(raw),"sha256":hashlib.sha256(raw).hexdigest(),
                         "source_id":f"{struct.unpack_from('<I',raw)[0]:08x}","version":struct.unpack_from('<I',raw,4)[0],"entries":records})
    return {"format":"EDDCDIRECTORY1","source":str(source),"archive_count":len(archives),"resource_count":sum(counts.values()),
            "counts":dict(sorted(counts.items())),"archives":archives,
            "scope":"Directory and payload provenance only; classification does not claim the PC payload decoders accept Dreamcast data."}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source",type=Path)
    parser.add_argument("output",type=Path)
    args=parser.parse_args()
    source,output=args.source.resolve(),args.output.resolve()
    if output==source or source in output.parents:parser.error("Manifest output must be outside extracted originals")
    if output.exists():parser.error("Manifest output already exists")
    data=inventory(source)
    with output.open("x",encoding="utf-8") as stream:stream.write(json.dumps(data,indent=2)+"\n")
    print(f"Checked {data['archive_count']} archives/{data['resource_count']} resources")


if __name__=="__main__":main()
