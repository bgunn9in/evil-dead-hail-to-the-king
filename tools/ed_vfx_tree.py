"""Original TreeBoss/TreeArm animation effects; raw34 is a PC no-op,35 is rumble."""
import hashlib
import json
import math
from pathlib import Path
import struct

from ed_actions import _va_reader,parse_actor_actions
from ed_assets import parse_archive
from ed_model import decode_animations
from ed_vfx_hits import parse_hit_effects,encode_hit_effects,ReferenceHits
from ed_vfx_emitters import parse_emitter_effects


def parse_tree_effects(exe):
    blank=parse_hit_effects(exe)[1] # Verified original executable hash.
    read=_va_reader(exe);f=lambda va:struct.unpack("<f",read(va,4))[0]
    u=lambda va:struct.unpack("<I",read(va,4))[0]
    cap=lambda kind:struct.unpack("<i",read(0x46a724+kind*4,4))[0]
    common={**blank,"velocity":[0,0,0],"size_min":0,"size_max":0,"growth":0,"delta":[0]*4,
            "frame_mode":0,"frame_delay":0,"direction_scale":0,"required_mode":0,
            "random_child":0,"random_child_chance":0}
    debris={**common,"kind":15,"constructor":0x428D30,"cap":cap(15),"life":u(0x428EB6+3),
        "color":[128,128,128,255],"size":f(0x428DA7+3),"size_min":f(0x428DAE+3),"size_max":f(0x428DB5+3),
        "dispersion":[f(0x428D56+1),f(0x428DC8+3),f(0x428D56+1)],"acceleration":[0,f(0x428E53+3),0],
        "uv":[0,f(0x428E10+3),f(0x428E09+3),f(0x428E17+3)],"columns":1,"rows":1,"random_frame":0,
        "direction_scale":1,"count_min":u(0x4120C5+3),"count_max":u(0x4120CF+3),"collision":2,
        "bounce_percent":[f(0x428E8A+1)]*3,"random_child":0,"random_child_chance":read(0x428ED6+3,1)[0]}
    corona={**common,"kind":24,"constructor":0x42A2B0,"cap":cap(24),"life":u(0x42A3B3+3),
        "color":[255]*4,"size":f(0x42A303+3),"uv":[0,f(0x42A35B+3),f(0x42A354+3),f(0x42A362+3)],
        "columns":4,"rows":2,"random_frame":1,"frame_mode":2}
    rings=[]
    for mode in (0,2):
        row=struct.unpack("<6f8B",read(0x46F078+mode*32,32))
        rings.append({"mode":mode,"ring":list(row[:6]),"ring_color":list(row[6:9]),"ring_segments":row[12]})
    row=read(0x46F238+4*20,20)
    return {"definitions":[debris,corona],"rings":rings,"corona_mode":4,"corona_row":row.hex()}


def encode_tree_effects(source):
    data=bytearray(struct.pack("<8sI",b"EDVFXT1\0",len(source["definitions"])))
    for definition in source["definitions"]:
        data.extend(encode_hit_effects([definition])[12:])
        data.extend(struct.pack("<2I",definition["random_child"],definition["random_child_chance"]))
    data.extend(struct.pack("<I",len(source["rings"])))
    for ring in source["rings"]:
        data.extend(struct.pack("<I6f4B",ring["mode"],*ring["ring"],*ring["ring_color"],ring["ring_segments"]))
    data.extend(struct.pack("<I",source["corona_mode"]));data.extend(bytes.fromhex(source["corona_row"]))
    return bytes(data)


def tree_animation_events(game):
    game=Path(game);exe=(game/"EvilDead.exe").read_bytes();blob=(game/"data/at02.pc").read_bytes();result=[]
    for actor,resources in (("treeboss",(0x034E2000,0x03CE2284)),("treearm",(0x034C0000,0x03CC0F04))):
        states=parse_actor_actions(exe,blob,actor);referenced={s["animation_id"]:s["index"]for s in states}
        for entry in parse_archive(blob):
            if entry.resource_id not in resources:continue
            data=blob[entry.offset:entry.offset+entry.size];clips=decode_animations(data);starts=struct.unpack_from(f"<{len(clips)}I",data,4)
            for clip,start in zip(clips,starts):
                if clip["resource_id"] not in referenced:continue # Orphan clips are preserved elsewhere, not asserted reachable.
                events=start+40+clip["frame_count"]*(3+clip["bone_count"]*3)*4
                result.append({"actor":actor,"state":referenced[clip["resource_id"]],"clip":clip["resource_id"],
                    "frame_count":clip["frame_count"],"events36":clip["events36_raw"],"events20":clip["events20_raw"],
                    "archive_offset36":entry.offset+events,"archive_offset20":entry.offset+events+len(clip["events36_raw"])*36})
    return {"archive_sha256":hashlib.sha256(blob).hexdigest(),"clips":result}


class ReferenceTree(ReferenceHits):
    def __init__(self,definitions,random):
        super().__init__(definitions,random)
        self.modes={15:0,24:0,25:0};self.sampled=None

    def spawn(self,kind,point,endpoint=None,mode=0):
        temporary_override=kind==15 and mode==1
        if kind in self.modes:
            if mode!=-1:self.modes[kind]=mode
            mode=self.modes[kind]
        else:mode=self.definitions[kind].get("required_mode",0)
        before=self.serial;definition=self.definitions[kind]
        if temporary_override:
            self.definitions[kind]={**definition,"color":[255]*4,"dispersion":[.09999999403953552,1.0499999523162842,.09999999403953552]}
        try:result=super().spawn(kind,point,endpoint)
        finally:self.definitions[kind]=definition
        for p in self.active.get(kind,[]):
            if p["serial"]>before:p.update(mode=mode,deferred=False)
        return result

    def update(self):
        for p in self.flat():p["deferred"]=False
        super().update()

    def sample_render(self,tick,visible=lambda p:True):
        if self.sampled==tick:return
        self.sampled=tick
        for p in self.active.get(24,[]):
            if visible(p["position"])and p["age"]%16 and self.spawn(22,p["position"]):
                self.active[22][0]["deferred"]=True


def corona_geometry(source,point,age):
    """42783F geometry/UV/alpha; compatibility repair interprets RGB as colors."""
    from ed_vfx import _f32 as f
    base,growth,*color=struct.unpack("<2f4B",bytes.fromhex(source["corona_row"])[:12])
    radius=f(base+age*growth*.0625);angle=age*f(.01745329238474369)
    u0,u1=f((112-16*math.sin(angle))/256),f((112+16*math.sin(angle))/256)
    v0,v1=f((208+16*math.cos(angle))/256),f((208+16*math.sin(angle))/256)
    tint=[max(0,min(255,c-((c*age)>>4)))/255 for c in color[:3]]
    tint.append((((0xFF000000-((age<<28)&0xffffffff))&0xffffffff)>>24)/255)
    result=[];step=f(math.pi)/8
    for i in range(4):
        a,b=f(f(math.pi/2)-i*step),f(math.pi/2)-(i+1)*step
        y0,r0=f(math.sin(a)*radius),f(math.cos(a)*radius)
        y1,r1=f(math.sin(b)*radius),f(math.cos(b)*radius)
        previous0=[point[0],f(point[1]+y0),f(point[2]+r0)];previous1=[point[0],f(point[1]+y1),f(point[2]+r1)]
        for j in range(9):
            theta=0 if j==8 else f(2*math.pi)/8*j;sine=f(math.sin(theta));cosine=math.cos(theta)
            current0=[f(point[0]+sine*r0),f(point[1]+y0),f(point[2]+cosine*r0)]
            current1=[f(point[0]+sine*r1),f(point[1]+y1),f(point[2]+cosine*r1)]
            quad=[[*previous0,u0,v0],[*current0,u1,v0],[*current1,u1,v1],[*previous1,u0,v1]]
            result.extend(quad[index]for index in(0,1,2,0,2,3));previous0,previous1=current0,current1
    return tint,result


def tree_requests(game):
    source=tree_animation_events(game)
    arm=next(c for c in source["clips"]if c["actor"]=="treearm"and c["state"]==1)
    death=next(c for c in source["clips"]if c["actor"]=="treeboss"and c["state"]==5)
    find=lambda records,kind,mode:next(bytes.fromhex(raw)for raw in records if struct.unpack_from("<Bi",bytes.fromhex(raw),3)==(1,mode)
        and bytes.fromhex(raw)[2]==kind)
    return [(0,find(arm["events36"],9,-1)),(0,find(arm["events36"],15,0)),(0,find(arm["events36"],25,2)),
        (2,find(death["events36"],24,4)),(3,find(death["events36"],25,0)),
        (4,find(death["events36"],15,2)),(5,find(death["events36"],15,-1))]


def tree_goldens(game,source):
    from ed_vfx import SourceRandom,_f32 as f,parse_effects
    from ed_vfx_ranged import parse_ranged_effects
    exe=(Path(game)/"EvilDead.exe").read_bytes();blank=parse_hit_effects(exe)[1]
    definitions=[parse_emitter_effects(exe)[0],{**blank,**parse_effects(exe)[0]},*source["definitions"],parse_emitter_effects(exe)[1],parse_ranged_effects(exe)[1]]
    random=SourceRandom();reference=ReferenceTree(definitions,random);requests=tree_requests(game);frames=104
    data=bytearray(struct.pack("<8s2I",b"EDVFXTG1",frames,len(requests)))
    for tick,raw in requests:data.extend(struct.pack("<I",tick));data.extend(raw)
    for tick in range(frames):
        for when,raw in requests:
            if when!=tick:continue
            kind=raw[2];mode=struct.unpack_from("<i",raw,4)[0];a=struct.unpack_from("<3f",raw,8);b=struct.unpack_from("<3f",raw,20)
            # Identity model XYZ rotation, retaining nativeMirrorX andWORLD translation.
            place=lambda v:[f(1.25-v[0]),f(.75+v[1]),f(-2+v[2])]
            reference.spawn(kind,place(a),place(b),mode)
        if tick:reference.update()
        reference.sample_render(tick)
        particles=reference.flat();data.extend(struct.pack("<5I64I3i",tick,len(particles),random.seed,random.index,int(random.ready),*random.table,
            *(reference.modes[k]for k in (15,24,25))))
        for p in particles:data.extend(struct.pack("<IQ7fiI4B3BBiB",p["kind"],p["serial"],*p["position"],*p["velocity"],p["size"],p["age"],p["tile"],*p["color"],*p["rotation"],int(p["stopped"]),p["mode"],int(p["deferred"])))
    data.extend(struct.pack("<I",4));initial=SourceRandom().cached(8)
    base,growth=struct.unpack_from("<2f",bytes.fromhex(source["corona_row"]))
    for age in (0,1,8,16):
        data.extend(struct.pack("<I",age));size=f(base+age*growth*.125)
        tile=(initial+age)%8;u=(tile%4)*.0625;v=.5+(tile//4)*.125
        for glow in (False,True):
            tint=(1,1,128/255,64/255)if glow else(1,1,1,1);data.extend(struct.pack("<4fI",*tint,6));quad=[]
            for x,y in ((-1,-1),(1,-1),(1,1),(-1,1)):
                quad.append([f(f(.3)+x*size),f(f(1.4)+(y*size if glow or y<0 else 0)),f(-2),
                    (.5 if x>0 else .375)if glow else u+(x>0)*.0625,
                    (.875 if y>0 else .75)if glow else v+(y>0)*.0625])
            for index in (0,1,2,0,2,3):data.extend(struct.pack("<5f",*quad[index]))
        tint,vertices=corona_geometry(source,[f(.3),f(1.4),-2.],age)
        data.extend(struct.pack("<4fI",*tint,len(vertices)))
        for vertex in vertices:data.extend(struct.pack("<5f",*vertex))
    return bytes(data)


def export_tree_effects(game,output):
    game,output=Path(game).resolve(),Path(output).resolve()
    if game==output or game in output.parents:raise ValueError("Tree VFX export must stay outside originals")
    target=output/"vfx"
    if output not in target.resolve().parents:raise ValueError("Tree VFX output escapes generated assets")
    source=parse_tree_effects((game/"EvilDead.exe").read_bytes());target.mkdir(parents=True,exist_ok=True)
    (target/"tree-effects.bin").write_bytes(encode_tree_effects(source));goldens=tree_goldens(game,source)
    (target/"tree-goldens.bin").write_bytes(goldens)
    return {"kinds":[15,24],"definitions":source["definitions"],"ring_modes":[0,2,3],"ring_variants":source["rings"],"corona_row":source["corona_row"],"golden_bytes":len(goldens),"source_events":tree_animation_events(game),
        "compatibility_repair":"Secondary24 interprets computed/clampedRGB as colorvalues, replacing original42795E/966/975 neighboring-memoryreads. Sourcegeometry,UV,alpha,timingandrendertrail22 retained; colors intentionallyarenotbit-identical to PC garbage reads."}
