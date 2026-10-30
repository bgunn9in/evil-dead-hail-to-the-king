"""Original Ash raw particle definitions, source callbacks and independent goldens."""
import hashlib
from pathlib import Path
import struct

from ed_actions import _va_reader,parse_actor_actions
from ed_assets import parse_archive
from ed_model import decode_animations
from ed_scripts import ScriptPackage
from ed_vm import Interpreter,Object,Array
from ed_vfx_hits import parse_hit_effects,encode_hit_effects,ReferenceHits


def parse_ash_effects(exe):
    blank=parse_hit_effects(exe)[1]
    read=_va_reader(exe);f=lambda va:struct.unpack("<f",read(va,4))[0];u=lambda va:struct.unpack("<I",read(va,4))[0]
    return [{**blank,"kind":20,"constructor":0x428A10,"cap":struct.unpack("<i",read(0x46A724+80,4))[0],
        "life":0,"color":[128,128,128,232],"delta":[0,0,0,-2],"velocity":[0,0,0],
        "acceleration":[0,f(0x428B37+3),0],"dispersion":[f(0x428A39+1),0,f(0x428A39+1)],
        "direction_scale":0,"size":0,"size_min":f(0x428A93+3),"size_max":f(0x428A9A+3),"growth":0,
        "uv":[f(0x428AE7+2),f(0x428AF4+3),f(0x428AED+3),f(0x428AFB+3)],
        "columns":1,"rows":4,"random_frame":1,"frame_mode":0,"frame_delay":0,
        "count_min":u(0x411F64+3),"count_max":u(0x411F6E+3),"collision":1,"child_death":18,
        "rotation_step":[0,0,0],"bounce_percent":[100]*3,"random_child":18,"random_child_chance":5}]


def encode_ash_effects(definitions):
    data=bytearray(struct.pack("<8sI",b"EDVFXA1\0",len(definitions)))
    for d in definitions:
        data.extend(encode_hit_effects([d])[12:]);data.extend(struct.pack("<2I",d["random_child"],d["random_child_chance"]))
    return bytes(data)


def parse_shell_effects(exe):
    blank=parse_hit_effects(exe)[1];read=_va_reader(exe);f=lambda va:struct.unpack("<f",read(va,4))[0]
    return {**blank,"kind":8,"constructor":0x428280,"cap":struct.unpack("<i",read(0x46A724+32,4))[0],
        "life":0,"color":[64,64,64,255],"delta":[0]*4,"velocity":[0]*3,
        "acceleration":[0,f(0x4283A0+3),0],"dispersion":[f(0x4282AF+1)]*3,
        "direction_scale":1,"size":25.,"size_min":0,"size_max":0,"growth":0,
        "uv":[f(0x428350+2),f(0x42835D+3),f(0x428356+3),f(0x428364+3)],"columns":1,"rows":1,
        "random_frame":0,"frame_mode":0,"frame_delay":0,"count_min":1,"count_max":1,"collision":2,"child_death":0,
        "rotation_step":[0]*3,"bounce_percent":[25.]*3,"bounce_flags":1,"random_child":0,"random_child_chance":0}


def parse_flash_effects(exe):
    """4285F0/4122CB: immediate one-particle6, no persistent emitter or RNG."""
    blank=parse_hit_effects(exe)[1];read=_va_reader(exe);f=lambda va:struct.unpack("<f",read(va,4))[0]
    return {**blank,"kind":6,"constructor":0x4285F0,"cap":struct.unpack("<i",read(0x46A724+24,4))[0],
        "life":struct.unpack("<I",read(0x42871D+3,4))[0],"color":[255,255,255,read(0x42864C+3,1)[0]],
        "delta":[0,0,0,struct.unpack("<b",read(0x42865F+3,1))[0]],"velocity":[0]*3,"acceleration":[0]*3,
        "dispersion":[0]*3,"direction_scale":0,"size":f(0x428663+3),"size_min":0,"size_max":0,"growth":f(0x42866A+3),
        "uv":[f(0x4286C0+2),f(0x4286C9+3),f(0x4286AE+1),f(0x4286AE+1)],"columns":1,"rows":1,
        "random_frame":0,"frame_mode":0,"frame_delay":0,"count_min":1,"count_max":1,"collision":0,"child_death":0,
        "rotation_step":[0]*3,"bounce_percent":[100]*3,"random_child":0,"random_child_chance":0}


def flash_goldens(game):
    from ed_vfx import SourceRandom,_f32 as f
    from ed_vfx_variants import _billboard
    game=Path(game);definition=parse_flash_effects((game/"EvilDead.exe").read_bytes());requests=list(zip((0,2),ash_animation_events(game,{6})[:2]))
    random=SourceRandom();reference=ReferenceHits([definition],random);data=bytearray(struct.pack("<8s2I",b"EDVFXFG1",12,len(requests)))
    for tick,event in requests:data.extend(struct.pack("<I",tick)+bytes.fromhex(event["raw"]))
    for tick in range(12):
        for when,event in requests:
            if when==tick:
                a=struct.unpack_from("<3f",bytes.fromhex(event["raw"]),8);reference.spawn(6,[f(1-a[0]),f(.75+a[1]),f(-2+a[2])])
        if tick:reference.update()
        particles=reference.flat();data.extend(struct.pack("<2I",tick,len(particles)))
        for p in particles:
            data.extend(struct.pack("<IQ4fi4B",6,p["serial"],*p["position"],p["size"],p["age"],*p["color"]))
            for scale,uv,tint in((f(.001),definition["uv"],[c/255 for c in p["color"]]),
                                  (f(.002),[.125,.75,.25,.875],[1,1,128/255,64/255])):
                data.extend(struct.pack("<4f",*tint))
                for vertex in _billboard(p["position"],f(p["size"]*scale),uv):data.extend(struct.pack("<5f",*vertex))
    if random.seed!=12345 or random.ready:raise ValueError("Original flash6 unexpectedly consumed RNG")
    return bytes(data)


def shell_repulsion(position,velocity,actors):
    from ed_vfx import _f32 as f
    if len(actors)!=13:raise ValueError("Source shell force requires thirteen physical slots")
    result=list(velocity)
    for actor in actors:
        if actor is None or not actor["flags"]&1 or actor["flags"]&8:continue
        local=actor["local"];dx=f(local[0]-position[0]);dz=f(local[2]-position[2]);dy=f(local[1]+actor["floor"])-position[1]
        squared=((local[2]-position[2])*dz+dy*dy)+dx*dx
        if squared>=f(.15):continue
        scale=3/(f(squared+f(.00001))**.5)
        result[0]=f(result[0]+scale*f(.005)*(1 if dx<0 else -1))
        result[1]=f(result[1]+scale*f(.001));result[2]=f(result[2]+scale*f(.005)*(1 if dz<0 else -1))
    return result


def shell_event(game):
    events=ash_animation_events(game,{8})
    if not events:raise ValueError("Original reachable Ash shell event is missing")
    return events[0]


def shell_goldens(game):
    from ed_vfx import SourceRandom,_f32 as f
    game=Path(game);definition=parse_shell_effects((game/"EvilDead.exe").read_bytes());raw=bytes.fromhex(shell_event(game)["raw"])
    random=SourceRandom();reference=ReferenceHits([definition],random);a,b=struct.unpack_from("<3f",raw,8),struct.unpack_from("<3f",raw,20)
    first=[-a[0],a[1],a[2]];second=[-b[0],b[1],b[2]];actors=[None]*13
    actors[0]={"flags":1,"local":[0.,0.,0.],"floor":0.}
    actors[1]={"flags":0x41,"local":[f(first[0]+.1),0.,first[2]],"floor":first[1]}
    actors[12]={"flags":9,"local":list(first),"floor":0.}
    data=bytearray(struct.pack("<8sI",b"EDVFXSG1",240)+raw)
    for actor in actors:data.extend(struct.pack("<I4f",actor["flags"],*actor["local"],actor["floor"])if actor else struct.pack("<I4f",0,0,0,0,0))
    for tick in range(240):
        if tick in(0,3):reference.spawn(8,first,second)
        if tick:
            for p in reference.active.get(8,[]):
                p["velocity"]=[f(v+a)for v,a in zip(p["velocity"],definition["acceleration"])]
                p["velocity"]=shell_repulsion(p["position"],p["velocity"],actors)
                if not p["stopped"]:p["position"]=[f(x+v)for x,v in zip(p["position"],p["velocity"])]
                reference.coarse_calls+=1
                if p["position"][1]<0:
                    reference.floor_calls+=1;p["position"][1]=0.;p["velocity"][1]=-p["velocity"][1]
                    if f(-.01)<p["velocity"][1]<f(.01):
                        p["velocity"][1]=0.
                        if not definition["bounce_flags"]:p["stopped"]=True
                    for axis in range(3):
                        v=p["velocity"][axis]*25*f(.01)
                        p["velocity"][axis]=0. if axis!=1 and f(-.01)<v<f(.01)else f(v)
                p["age"]+=1
        particles=reference.flat();data.extend(struct.pack("<7I64I",tick,len(particles),random.seed,random.index,int(random.ready),reference.coarse_calls,reference.floor_calls,*random.table))
        for p in particles:data.extend(struct.pack("<IQ7fiI4B?",p["kind"],p["serial"],*p["position"],*p["velocity"],p["size"],p["age"],p["tile"],*p["color"],p["stopped"]))
    return bytes(data)


def ash_animation_events(game,kinds):
    game=Path(game);exe=(game/"EvilDead.exe").read_bytes();blob=(game/"data/az01.pc").read_bytes()
    used={s["animation_id"]for s in parse_actor_actions(exe,blob,"ash")};result=[]
    for entry in parse_archive(blob):
        if entry.resource_id not in(0x01024000,0x01824184):continue
        payload=blob[entry.offset:entry.offset+entry.size];clips=decode_animations(payload);starts=struct.unpack_from(f"<{len(clips)}I",payload,4)
        for clip,start in zip(clips,starts):
            if clip["resource_id"]not in used:continue
            offset=entry.offset+start+40+clip["frame_count"]*(3+3*clip["bone_count"])*4
            for index,raw in enumerate(clip["events36_raw"]):
                record=bytes.fromhex(raw)
                if record[2]not in kinds or not record[3]or struct.unpack_from("<H",record)[0]>=clip["frame_count"]:continue
                result.append({"clip":clip["resource_id"],"archive_offset":offset+index*36,"raw":raw})
    return result


def ash_particle_events(game):
    result=ash_animation_events(game,{20})
    if any(struct.unpack_from("<i",bytes.fromhex(e["raw"]),4)[0]!=-1 for e in result):raise ValueError("Unrecovered Ash20 emitter mode")
    return result


def source_combat_counter(game):
    data=(Path(game)/"data/script/pc/levelA.jpc").read_bytes();package=ScriptPackage(data);vm=Interpreter(package);vm.initialize("Ash")
    ash=Object(next(c["index"]for c in package.classes if c["name"]=="Ash"),{5:0,6:0})
    vm.statics[53]=Array('L',[ash]);vm.statics[54]=Array('I',[0]*40)
    invoke=lambda:vm.invoke(648,args=(0,0,20,0,0,0,0,0,0,0,0))
    if invoke()!=1 or vm.statics[72]!=0:raise ValueError("Original idle trail suppression changed")
    vm.invoke(684,ash)
    if vm.statics[72]!=100:raise ValueError("Original combo trail increment changed")
    accepted=0
    while not invoke():
        accepted+=1
        if accepted>100:raise ValueError("Unbounded original combat trail")
    if accepted!=100 or vm.statics[72]!=0:raise ValueError("Original trail consumption changed")
    methods=[{**package.methods[i],"file_offset":56+package.methods[i]["code_offset"]}for i in(648,670,684,691)]
    return {"source_sha256":hashlib.sha256(data).hexdigest(),"initial_suppressed":True,"accepted_after_combo":accepted,
        "counter_after":vm.statics[72],"then_suppressed":True,"methods":methods}


def source_misc_callbacks(game):
    packages=[]
    for level in('A','B'):
        data=(Path(game)/f"data/script/pc/level{level}.jpc").read_bytes();package=ScriptPackage(data);vm=Interpreter(package)
        vm.initialize("Ash");vm.initialize("RumbleCommander");ash=Object(next(c["index"]for c in package.classes if c["name"]=="Ash"),{5:0,6:0})
        vm.statics[53]=Array('L',[ash]);vm.statics[54]=Array('I',[0]*40);rows=[]
        callback=next(m["index"]for m in package.methods if m["class_name"]=="ActorDB"and m["name"]=="processVFXAction")
        for classification in(0,1,2,3):
            vm.statics[762]=classification
            for material in(0,1,6,8):
                result=vm.invoke(callback,args=(0,0,9,material,2,3,4,0,0,0,0))
                if result!=int(classification==2 and material==0):raise ValueError("Original Ash9 material predicate changed")
                rows.append({"classification_static762":classification,"first_material":material,"consumed":bool(result)})
        methods=[]
        for owner,name in(("ActorDB","processVFXAction"),("Ash","processVFXAction"),("RumbleCommander","engineReportRumbleVFX")):
            m=next(m for m in package.methods if m["class_name"]==owner and m["name"]==name);start=56+m["code_offset"]
            methods.append({**m,"file_offset":start,"code_sha256":hashlib.sha256(data[start:start+m["code_length"]]).hexdigest()})
        packages.append({"level":level,"sha256":hashlib.sha256(data).hexdigest(),"methods":methods,"predicate_cases":rows})
    return {"packages":packages,"events":ash_animation_events(game,{0,9,35})}


class ReferenceAsh(ReferenceHits):
    def update(self):
        from ed_vfx import _f32 as f
        for kind in sorted(self.definitions):
            d=self.definitions[kind];particles=self.active.get(kind,[])
            for p in particles:
                p["velocity"]=[f(v+a)for v,a in zip(p["velocity"],d["acceleration"])]
                p["position"]=[f(x+v)for x,v in zip(p["position"],p["velocity"])]
            if d["collision"]:
                for p in particles:
                    x,y,z=p["position"];self.coarse_calls+=1;coarse=self.coarse(x,z)
                    if y<coarse or(coarse==32767 and y< -100):
                        self.floor_calls+=1;p["position"][1]=f(self.floor(x,coarse,z));p["age"]=-1
            if d["life"]:
                for p in particles:
                    if p["age"]>d["life"]:p["age"]=-1
            for p in particles:
                if d["child_death"]and p["age"]==-1:self.spawn(d["child_death"],p["position"])
                elif d.get("random_child_chance")and self.random.cached(100)<d["random_child_chance"]:self.spawn(d["random_child"],p["position"])
        for kind,particles in self.active.items():
            d=self.definitions[kind];self.active[kind]=[p for p in particles if p["age"]>=0]
            for p in self.active[kind]:
                p["color"]=[min(255,max(0,c+delta))for c,delta in zip(p["color"],d["delta"])]
                p["age"]=((p["age"]+1+32768)&65535)-32768;p["size"]=f(p["size"]+d["growth"])


def ash_goldens(game,definition):
    from ed_vfx import SourceRandom,_f32
    game=Path(game);event=ash_particle_events(game)[0];raw=bytes.fromhex(event["raw"]);random=SourceRandom()
    reference=ReferenceAsh([parse_hit_effects((game/"EvilDead.exe").read_bytes())[1],*definition],random)
    point=struct.unpack_from("<3f",raw,8);world=[_f32(1-point[0]),_f32(1.25+point[1]),_f32(-2+point[2])]
    data=bytearray(struct.pack("<8sI",b"EDVFXAG1",140)+raw)
    for tick in range(140):
        if tick in(0,3):reference.spawn(20,world)
        if tick:reference.update()
        particles=reference.flat();data.extend(struct.pack("<7I64I",tick,len(particles),random.seed,random.index,int(random.ready),reference.coarse_calls,reference.floor_calls,*random.table))
        for p in particles:data.extend(struct.pack("<IQ7fiI4B",p["kind"],p["serial"],*p["position"],*p["velocity"],p["size"],p["age"],p["tile"],*p["color"]))
    return bytes(data)


def export_ash_effects(game,output):
    import os,tempfile
    game,output=Path(game).resolve(),Path(output).resolve()
    if game==output or game in output.parents:raise ValueError("Ash VFX export must stay outside originals")
    target=output/"vfx"
    if output not in target.resolve().parents:raise ValueError("Ash VFX export escapes generated assets")
    exe=(game/"EvilDead.exe").read_bytes();definitions=[*parse_ash_effects(exe),parse_shell_effects(exe),parse_flash_effects(exe)];target.mkdir(parents=True,exist_ok=True)
    for name,data in(("ash-effects.bin",encode_ash_effects(definitions)),("ash-goldens.bin",ash_goldens(game,definitions)),("shell-goldens.bin",shell_goldens(game)),("flash-goldens.bin",flash_goldens(game))):
        path=target/name
        if output not in path.resolve().parents:raise ValueError("Ash VFX export escapes generated assets")
        with tempfile.NamedTemporaryFile('wb',dir=target,delete=False)as stream:stream.write(data);temporary=stream.name
        try:os.replace(temporary,path)
        finally:
            if os.path.exists(temporary):os.unlink(temporary)
    return {"definitions":definitions,"combat_counter":source_combat_counter(game),"shell_event":shell_event(game),"flash_events":ash_animation_events(game,{6}),
        "flash_physics":"Native4122CB one immediate6, no RNG; emitter disabled afterwards. Life4,size50+10/tick,alpha128-32/tick;425CF0 dual billboards sourceUV and fixed glowUV,scales.001/.002.",
        "shell_physics":"Original4102B0 physical13 actor forces before410760 motion, then40FF80 LOW16 floor bounce/stop. No force alias or absent-actor fallback."}
