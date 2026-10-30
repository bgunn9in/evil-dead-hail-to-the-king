"""Original LevelA ring25/corona24 mode1 and its real child emitters3/32."""
import hashlib
from pathlib import Path
import struct

from ed_actions import _va_reader,parse_actor_actions
from ed_assets import parse_archive
from ed_model import decode_animations
from ed_vfx_hits import parse_hit_effects,encode_hit_effects
from ed_vfx_tree import ReferenceTree


def parse_variant_effects(exe):
    blank=parse_hit_effects(exe)[1];read=_va_reader(exe)
    f=lambda va:struct.unpack("<f",read(va,4))[0]
    cap=lambda kind:struct.unpack("<i",read(0x46A724+kind*4,4))[0]
    common={**blank,"velocity":[0]*3,"direction_scale":1,"size":0,"random_frame":1,"growth":0,
        "frame_mode":2,"frame_delay":0,"child_death":0,"rotation_step":[0]*3,"bounce_flags":0,
        "random_child":0,"random_child_chance":0}
    fire={**common,"kind":3,"constructor":0x429C30,"cap":cap(3),"life":11,
        "count_min":4,"count_max":10,"color":[255]*4,"delta":[0,-16,-8,-18],
        "size_min":f(0x429CB2+3),"size_max":f(0x429CB9+3),"growth":f(0x429CC0+3),
        "dispersion":[f(0x429CD0+3),f(0x429C59+1),f(0x429C59+1)],"acceleration":[0,f(0x429D62+3),0],
        "uv":[f(0x429D12+2),f(0x429D1F+3),f(0x429D18+3),f(0x429D26+3)],
        "columns":4,"rows":4,"collision":2,"bounce_percent":[10]*3,"children":[]}
    glow={**common,"kind":32,"constructor":0x42AB10,"cap":cap(32),"life":0,
        "count_min":1,"count_max":1,"color":[255]*4,"delta":[0]*4,
        "size_min":f(0x42AB8E+3),"size_max":f(0x42AB97+3),"growth":1,
        "dispersion":[f(0x42AB6D+1),f(0x42ABAD+3),f(0x42AB6D+1)],"acceleration":[0,f(0x42AC3B+3),0],
        "uv":[0,f(0x42ABF8+3),f(0x42ABF1+3),f(0x42ABFF+3)],"columns":4,"rows":2,
        "collision":1,"bounce_percent":[100]*3,
        "children":[{"flags":4,"kind":0,"chance":25},{"flags":4,"kind":22,"chance":15},{"flags":1,"kind":0,"chance":0}]}
    ring=struct.unpack("<6f8B",read(0x46F078+32,32));corona=read(0x46F238+20,20)
    script_rings=[]
    for mode in(11,12):
        row=struct.unpack("<6f8B",read(0x46F078+32*mode,32));script_rings.append({"mode":mode,"curve":list(row[:6]),"color":list(row[6:9]),"segments":row[12]})
    raw6=read(0x46F238+120,20);script_corona={"mode":6,"raw":raw6.hex(),"size":list(struct.unpack_from("<2f",raw6)),
        "color":list(raw6[8:11]),"fade":struct.unpack_from("<I",raw6,12)[0],"segments":raw6[17],"hemisphere":raw6[19]}
    return {"script_rings":script_rings,"script_corona":script_corona,"definitions":[fire,glow],"ring":{"mode":1,"curve":list(ring[:6]),"color":list(ring[6:9]),"segments":ring[12]},
        "corona":{"mode":1,"raw":corona.hex(),"size":list(struct.unpack_from("<2f",corona)),
            "color":list(corona[8:11]),"fade":struct.unpack_from("<I",corona,12)[0],
            "segments":corona[17],"hemisphere":corona[19]},"source_sha256":hashlib.sha256(exe).hexdigest()}


def encode_variant_effects(source):
    data=bytearray(struct.pack("<8sI",b"EDVFXV1\0",len(source["definitions"])))
    for d in source["definitions"]:
        data.extend(encode_hit_effects([d])[12:]);data.extend(struct.pack("<I",len(d["children"])))
        for child in d["children"]:data.extend(struct.pack("<4B",child["flags"],child["kind"],child["chance"],0))
    rings=[source["ring"],*source.get("script_rings",[])];data.extend(struct.pack("<I",len(rings)))
    for ring in rings:data.extend(struct.pack("<I6f4B",ring["mode"],*ring["curve"],*ring["color"],ring["segments"]))
    coronas=[source["corona"],*([source["script_corona"]]if"script_corona"in source else[])];data.extend(struct.pack("<I",len(coronas)))
    for corona in coronas:data.extend(struct.pack("<I",corona["mode"]));data.extend(bytes.fromhex(corona["raw"]))
    return bytes(data)


def variant_events(game):
    game=Path(game);exe=(game/"EvilDead.exe").read_bytes();result=[]
    for name,archive,packs,kind in(("deadash","ag03.pc",(0x030C0000,0x038C0284),25),
        ("matriarchboss","ah06.pc",(0x03310000,0x03B10144),24),("anniezombie","au04.pc",(0x03032000,0x03832284),24)):
        blob=(game/"data"/archive).read_bytes();used={s["animation_id"]for s in parse_actor_actions(exe,blob,name)}
        for entry in parse_archive(blob):
            if entry.resource_id not in packs:continue
            payload=blob[entry.offset:entry.offset+entry.size];clips=decode_animations(payload);starts=struct.unpack_from(f"<{len(clips)}I",payload,4)
            for clip,start in zip(clips,starts):
                if clip["resource_id"]not in used:continue
                base=entry.offset+start+40+clip["frame_count"]*(3+3*clip["bone_count"])*4
                for index,h in enumerate(clip["events36_raw"]):
                    raw=bytes.fromhex(h)
                    if raw[2]==kind and raw[3]and struct.unpack_from("<H",raw)[0]<clip["frame_count"]and struct.unpack_from("<i",raw,4)[0]==1:
                        result.append({"actor":name,"archive":archive,"archive_sha256":hashlib.sha256(blob).hexdigest(),
                            "clip":clip["resource_id"],"archive_offset":base+index*36,"raw":h})
    return result


class ReferenceVariants(ReferenceTree):
    def __init__(self,definitions,random,player=(0.,0.,0.)):
        super().__init__(definitions,random);self.pending={};self.player=player

    def _commit_pending(self):
        by_serial={p["serial"]:p for p in self.flat()}
        for serial,(position,age)in self.pending.items():
            by_serial[serial]["position"]=list(position);by_serial[serial]["age"]=age
        self.pending={}

    def update(self):
        self._commit_pending();super().update()

    def _render_child(self,kind,position):
        before=self.serial;result=self.spawn(kind,position)
        if kind<24:
            for p in self.active.get(kind,[]):
                if p["serial"]>before:p["deferred"]=True
        return result

    def sample_render(self,tick,visible=lambda p:True,advance=False):
        from ed_vfx import _f32 as f
        if self.sampled==tick and not advance:return
        if advance:
            self._commit_pending()
            for p in self.flat():p["deferred"]=False
        self.sampled=tick
        for p in self.active.get(24,[]):
            if not visible(p["position"]):continue
            if p["age"]%16:self._render_child(22,p["position"])
            if p["mode"]!=1:continue
            old=p["position"];dx=f(self.player[0]-old[0]);dz=f(self.player[2]-old[2])
            new=[f(old[0]+dx*.25),old[1],f(old[2]+dz*.25)]
            for kind in((0,3,32)if not p["age"]&1 else(3,3,3)):self._render_child(kind,new)
            self.pending[p["serial"]]=(new,-1 if abs(dx)<.5 and abs(dz)<.5 else p["age"])


def corona_secondary_geometry(corona,point,age,uv_age):
    """Native42783F; explicit compatibility repair uses computed RGB values."""
    import math
    from ed_vfx import _f32 as f
    radius=f(corona["size"][0]+age*corona["size"][1]*.0625);wide=uv_age*f(.01745329238474369)
    sine,cosine=math.sin(wide),math.cos(f(wide));left,right=f((112-16*sine)/256),f((112+16*sine)/256)
    top,bottom=f((208+16*cosine)/256),f((208+16*sine)/256)
    tint=[max(0,min(255,c-((c*age)>>4)))/255 for c in corona["color"]]
    tint.append((((0xff000000-((age<<28)&0xffffffff))&0xffffffff)>>24)/255)
    n=corona["segments"];latitudes=(n-1)//2+1 if corona["hemisphere"]else n;step=f(math.pi)/n;vertices=[]
    for i in range(latitudes):
        a,b=f(f(math.pi/2)-i*step),f(math.pi/2)-(i+1)*step
        y0,r0=f(math.sin(a)*radius),f(math.cos(a)*radius);y1,r1=f(math.sin(b)*radius),f(math.cos(b)*radius)
        previous0=[point[0],f(point[1]+y0),f(point[2]+r0)];previous1=[point[0],f(point[1]+y1),f(point[2]+r1)]
        for j in range(n+1):
            theta=0 if j==n else f(2*math.pi)/n*j;s=f(math.sin(theta));c=math.cos(theta)
            current0=[f(point[0]+s*r0),f(point[1]+y0),f(point[2]+c*r0)];current1=[f(point[0]+s*r1),f(point[1]+y1),f(point[2]+c*r1)]
            quad=[[*previous0,left,top],[*current0,right,top],[*current1,right,bottom],[*previous1,left,bottom]]
            vertices.extend(quad[k]for k in(0,1,2,0,2,3));previous0,previous1=current0,current1
    return tint,vertices


def _billboard(point,size,uv):
    from ed_vfx import _f32 as f
    u0,v0,u1,v1=uv;quad=[]
    for x,y in((-1,-1),(1,-1),(1,1),(-1,1)):
        quad.append([f(point[0]+x*size),f(point[1]+y*size),point[2],u1 if x>0 else u0,v1 if y>0 else v0])
    return [quad[k]for k in(0,1,2,0,2,3)]


def variant_geometry(reference,source,visible=True):
    from ed_vfx import _f32 as f
    from ed_vfx_emitters import ring_geometry
    result=[]
    if not visible:return result
    for kind in sorted(reference.active):
        d=reference.definitions[kind];decal=0.
        for p in reference.active[kind]:
            if p["deferred"]:continue
            tint=[c/255 for c in p["color"]];du=f((d["uv"][2]-d["uv"][0])/d["columns"]);dv=f((d["uv"][3]-d["uv"][1])/d["rows"])
            u=f(d["uv"][0]+(p["tile"]%d["columns"])*du);v=f(d["uv"][1]+(p["tile"]//d["columns"])*dv);uv=[u,v,f(u+du),f(v+dv)]
            if kind==25:
                mode=next(row for row in[source["ring"],*source.get("script_rings",[])]if row["mode"]==p["mode"]);result.append(ring_geometry({**d,"ring":mode["curve"],"ring_color":mode["color"],"ring_segments":mode["segments"]},p["position"],p["age"]));continue
            if kind==24:
                row=source["script_corona"]if p["mode"]==6 else source["corona"];size=f(row["size"][0]+p["age"]*row["size"][1]*.125)
                primary=_billboard(p["position"],size,uv)
                if row["hemisphere"]:
                    for vertex in primary:
                        if vertex[1]>p["position"][1]:vertex[1]=p["position"][1]
                        if vertex[4]>uv[1]:vertex[4]=f(uv[1]+(uv[3]-uv[1])*.5)
                result.append((tint,primary))
                result.append(([1,1,128/255,64/255],_billboard(p["position"],size,[.375,.75,.5,.875])))
                position,age=reference.pending.get(p["serial"],(p["position"],p["age"]))
                result.append(corona_secondary_geometry(row,position,age,p["age"]));continue
            if kind==32:
                result.append((tint,_billboard(p["position"],f(p["size"]*f(.001)),uv)))
                result.append(([1,1,128/255,64/255],_billboard(p["position"],f(p["size"]*f(.003)),[.375,.75,.5,.875])));continue
            if kind in(18,22):
                point=list(p["position"]);point[1]=f((0 if kind==22 else point[1])+decal);quad=[]
                for x,z in((-1,-1),(1,-1),(1,1),(-1,1)):
                    quad.append([f(point[0]+x*p["size"]),point[1],f(point[2]+z*p["size"]),uv[2]if x>0 else uv[0],uv[3]if z>0 else uv[1]])
                result.append((tint,[quad[k]for k in(0,1,2,0,2,3)]));decal=f(decal+f(.00002));continue
            result.append((tint,_billboard(p["position"],f(p["size"]*f(.001)),uv)))
    return result


def variant_goldens(game,source):
    from ed_vfx import SourceRandom,_f32 as f
    from ed_vfx_emitters import parse_emitter_effects
    from ed_vfx_ranged import parse_ranged_effects
    from ed_vfx_tree import parse_tree_effects
    game=Path(game);exe=(game/"EvilDead.exe").read_bytes();emitters=parse_emitter_effects(exe)
    definitions=[emitters[0],parse_hit_effects(exe)[1],parse_ranged_effects(exe)[1],parse_tree_effects(exe)["definitions"][1],emitters[1],*source["definitions"]]
    random=SourceRandom();reference=ReferenceVariants(definitions,random);events=variant_events(game)
    corona=next(e for e in events if e["actor"]=="matriarchboss");ring=next(e for e in events if e["actor"]=="deadash");annie=next(e for e in events if e["actor"]=="anniezombie")
    a=struct.unpack_from("<3f",bytes.fromhex(corona["raw"]),8)
    requests=[(0,corona,[1.25,.75,-2.]),(2,ring,[-1.,.5,2.]),(4,annie,[1.,.5,-1.]),(70,corona,[f(a[0]+.2),.5,f(.2-a[2])])]
    data=bytearray(struct.pack("<8s2I",b"EDVFXVG1",140,len(requests)))
    for tick,event,world in requests:data.extend(struct.pack("<I",tick)+bytes.fromhex(event["raw"])+struct.pack("<3f",*world))
    for tick in range(140):
        for when,event,world in requests:
            if when!=tick:continue
            raw=bytes.fromhex(event["raw"]);a=struct.unpack_from("<3f",raw,8);b=struct.unpack_from("<3f",raw,20)
            place=lambda v:[f(world[0]-v[0]),f(world[1]+v[1]),f(world[2]+v[2])]
            reference.spawn(raw[2],place(a),place(b),1)
        if tick:reference.update()
        reference.sample_render(tick);extra=2 if tick in(6,70)else 0
        for n in range(extra):reference.sample_render(tick,advance=True)
        particles=reference.flat();data.extend(struct.pack("<8I64I3i",tick,extra,len(particles),random.seed,random.index,int(random.ready),reference.coarse_calls,reference.floor_calls,*random.table,*(reference.modes[k]for k in(15,24,25))))
        for p in particles:data.extend(struct.pack("<IQ7fiI4B3BBiB",p["kind"],p["serial"],*p["position"],*p["velocity"],p["size"],p["age"],p["tile"],*p["color"],*p["rotation"],int(p["stopped"]),p["mode"],int(p["deferred"])))
        geometry=variant_geometry(reference,source)if tick in(0,1,6,70,71)else[];data.extend(struct.pack("<I",len(geometry)))
        for tint,vertices in geometry:
            data.extend(struct.pack("<4fI",*tint,len(vertices)))
            for vertex in vertices:data.extend(struct.pack("<5f",*vertex))
    return bytes(data)


def export_variant_effects(game,output):
    """Export only source-derived mode rows/children; never write into originals."""
    import os
    import tempfile
    game,output=Path(game).resolve(),Path(output).resolve()
    if output==game or game in output.parents:raise ValueError("Variant VFX export must stay outside originals")
    target=output/"vfx"
    if output not in target.resolve().parents:raise ValueError("Variant VFX output escapes generated assets")
    source=parse_variant_effects((game/"EvilDead.exe").read_bytes());goldens=variant_goldens(game,source)
    from ed_vfx_scene import scene_geometry_goldens,script_requests
    files={"variant-effects.bin":encode_variant_effects(source),"variant-goldens.bin":goldens,"script-goldens.bin":scene_geometry_goldens(game,source)}
    target.mkdir(parents=True,exist_ok=True)
    for name,data in files.items():
        path=target/name
        if output not in path.resolve().parents:raise ValueError("Variant VFX output escapes generated assets")
        with tempfile.NamedTemporaryFile("wb",dir=target,delete=False)as stream:
            stream.write(data);temporary=stream.name
        try:os.replace(temporary,path)
        finally:
            if os.path.exists(temporary):os.unlink(temporary)
    return {"definitions":source["definitions"],"ring":source["ring"],"corona":source["corona"],"script_rings":source.get("script_rings",[]),"script_corona":source.get("script_corona"),
        "source_sha256":source["source_sha256"],"events":variant_events(game),"script_requests":script_requests(game),
        "files":{name:{"bytes":len(data),"sha256":hashlib.sha256(data).hexdigest()}for name,data in files.items()},
        "render":"Source24 mode1: primary quads before homing; quarter-XZ homing, parity-dependent real children0/3/32; secondary sphere after homing using pre-homing UV age. Lower-group children are deferred one source render. Flag4 source render-only passes repeat producers without simulation; ordinary redraws remain pure.",
        "snapshot":"EDVFXS5 retains pending render mutations, deferred children, all RNG-independent timers and exact group order. Legacy S4 remains unchanged when optional variant assets are absent.",
        "compatibility_repair":"Secondary24 uses computed/clamped RGB values as colors instead of neighboring renderer memory; source geometry, UV, alpha and timing are preserved.",
        "maximum_vertices_per_particle":444,"pool_vertices_upper_bound":666000}
