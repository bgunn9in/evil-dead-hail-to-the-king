"""Original NPC16/17/21 particles and source callbacks; actor gates are separate."""
import hashlib
from pathlib import Path
import struct

from ed_actions import _va_reader,parse_actor_actions
from ed_assets import parse_archive
from ed_model import decode_animations
from ed_scripts import ScriptPackage
from ed_vm import Interpreter,Object,Array
from ed_vfx_hits import parse_hit_effects,encode_hit_effects,ReferenceHits


def parse_npc_effects(exe):
    blank=parse_hit_effects(exe)[1];read=_va_reader(exe)
    f=lambda va:struct.unpack("<f",read(va,4))[0]
    s=lambda va:struct.unpack("<b",read(va,1))[0]
    return [{**blank,"kind":17,"constructor":0x429240,"cap":struct.unpack("<i",read(0x46A724+68,4))[0],
        "life":struct.unpack("<I",read(0x4293A3+3,4))[0],"color":[255,255,255,read(0x42929C+3,1)[0]],
        "delta":[s(0x4292A3+3),0,0,s(0x4292B0+3)],"size":f(0x4292B4+3),"size_min":0,"size_max":0,
        "growth":f(0x4292BB+3),"velocity":[0]*3,"acceleration":[0]*3,"dispersion":[0]*3,"direction_scale":0,
        "count_min":1,"count_max":1,"columns":1,"rows":1,"random_frame":0,"frame_mode":0,"frame_delay":0,
        "uv":[0,0,f(0x4292FF+1),f(0x4292FF+1)],"collision":0,"child_death":0,
        "rotation_step":[0,read(0x429351+3,1)[0],0], # The other writes use known-zero BL, not immediates.
        "random_child":read(0x429383+3,1)[0],"random_child_chance":read(0x429387+3,1)[0]}]


def parse_blood_effect(exe):
    """428BA0/411E62: native21, distinct from Ash20 and hit2."""
    blank=parse_hit_effects(exe)[1];read=_va_reader(exe)
    f=lambda va:struct.unpack("<f",read(va,4))[0]
    u=lambda va:struct.unpack("<I",read(va,4))[0]
    return {**blank,"kind":21,"constructor":0x428BA0,"cap":struct.unpack("<i",read(0x46A724+84,4))[0],
        "life":0,"color":[255,255,255,read(0x428C07+3,1)[0]],"delta":[0,0,0,struct.unpack("<b",read(0x428C14+3,1))[0]],
        "velocity":[0]*3,"acceleration":[0,f(0x428CC6+3),0],"dispersion":[f(0x428BC6+1),f(0x428C39+3),f(0x428BC6+1)],
        "direction_scale":f(0x45D228),"size":0,"size_min":f(0x428C1B+3),"size_max":f(0x428C22+3),"growth":f(0x428C29+3),
        "count_min":u(0x411EF5+3),"count_max":u(0x411EFF+3),"columns":4,"rows":8,"random_frame":1,"frame_mode":0,"frame_delay":0,
        "uv":[f(0x428C7A+2),0,f(0x428C80+3),f(0x428C8A+3)],"collision":1,"child_death":read(0x428D23+3,1)[0],
        "rotation_step":[0]*3,"bounce_percent":[100]*3,"random_child":0,"random_child_chance":0}


def parse_sixteen_effect(exe):
    """428EF0/412184: native16 with age-gated positional jitter."""
    blank=parse_hit_effects(exe)[1];read=_va_reader(exe)
    f=lambda va:struct.unpack("<f",read(va,4))[0]
    u=lambda va:struct.unpack("<I",read(va,4))[0]
    b=lambda va:read(va,1)[0]
    return {**blank,"kind":16,"constructor":0x428EF0,"cap":struct.unpack("<i",read(0x46A724+64,4))[0],
        "life":u(0x4290A6+3),"color":[b(0x428F14+1),b(0x428F5C+3),b(0x428F14+1),b(0x428F60+3)],
        "delta":[b(0x428F64+3),0,0,struct.unpack("<b",read(0x428F6E+3,1))[0]],"velocity":[0]*3,
        "acceleration":[0,f(0x429019+3),0],"dispersion":[f(0x428F16+1),f(0x428F89+3),f(0x428F16+1)],
        "direction_scale":1.,"size":f(0x428F72+3),"size_min":0,"size_max":0,"growth":f(0x428F79+3),
        "count_min":u(0x412205+3),"count_max":u(0x41220F+3),"columns":1,"rows":1,"random_frame":1,"frame_mode":0,"frame_delay":0,
        "uv":[f(0x428FC9+2),f(0x428FD6+3),f(0x428FCF+3),f(0x428FDD+3)],"collision":2,"child_death":0,
        "rotation_step":[0]*3,"bounce_percent":[f(0x429050+1)]*3,"random_child":0,"random_child_chance":0,
        "position_jitter":[f(0x42907C+1)]*3,"jitter_min_age":b(0x429090+3),
        "children":[{"flags":b(0x4290BF+3),"kind":b(0x4290C3+3),"chance":b(0x4290C7+3)},
                    {"flags":b(0x4290E0+3),"kind":b(0x4290E4+3),"chance":b(0x4290E8+3)}]}


def sixteen_events(game):
    from export_actor import ACTORS,LEVEL_B_ACTORS
    rows=[]
    for level,profile,names in(("A",ACTORS,{"deadite":4,"matriarchboss":14}),("B",LEVEL_B_ACTORS,{"deadite":4})):
        for name,kind in names.items():
            d=profile[name]
            for event in npc_events(game,16,[(name,d["archive"],d["packs"])]):rows.append({"level":level,"native_type":kind,**event})
    return rows


def npc_events(game,kind=17,sources=None):
    game=Path(game);exe=(game/"EvilDead.exe").read_bytes();events=[]
    if sources is None:sources=(("deadite","az01.pc",(0x050C6000,0x058C6284)),("headlessskeleton","ak09.pc",(0x07082000,0x078820C4)))
    for actor,archive,packs in sources:
        blob=(game/"data"/archive).read_bytes();used={s["animation_id"]for s in parse_actor_actions(exe,blob,actor)}
        for entry in parse_archive(blob):
            if entry.resource_id not in packs:continue
            payload=blob[entry.offset:entry.offset+entry.size];clips=decode_animations(payload);starts=struct.unpack_from(f"<{len(clips)}I",payload,4)
            for clip,start in zip(clips,starts):
                if clip["resource_id"]not in used:continue
                offset=entry.offset+start+40+clip["frame_count"]*(3+3*clip["bone_count"])*4
                for index,h in enumerate(clip["events36_raw"]):
                    raw=bytes.fromhex(h)
                    if raw[2]!=kind or not raw[3]or struct.unpack_from("<H",raw)[0]>=clip["frame_count"]:continue
                    events.append({"actor":actor,"archive":archive,"archive_sha256":hashlib.sha256(blob).hexdigest(),
                        "clip":clip["resource_id"],"archive_offset":offset+36*index,"raw":h})
    return events


COMBAT_CALLERS={
    "A":{"skullbat":(2,"SkullBat"),"deadite":(4,"Deadite"),"troopknife":(7,"TroopKnife"),
         "troopclaw":(8,"TroopClaw"),"trooprange":(9,"TroopRange"),"troopleaderboss":(10,"TroopLeaderBoss"),
         "hellbillyfat":(11,"HellbillyFat"),"hellbillythin":(12,"HellbillyThin")},
    "B":{"skullbat":(2,"SkullBat"),"deadite":(4,"Deadite"),"guildmember":(23,"GuildMember")}}


def combat_events(game):
    from export_actor import ACTORS,LEVEL_B_ACTORS
    rows=[]
    for level,profile in(("A",ACTORS),("B",LEVEL_B_ACTORS)):
        for name,(kind,owner)in COMBAT_CALLERS[level].items():
            d=profile[name]
            for event in npc_events(game,20,[(name,d["archive"],d["packs"])]):
                rows.append({"level":level,"native_type":kind,"script_class":owner,**event})
    return rows


def combat_dispatch_audit(game):
    """Prove actual derived-class virtual dispatch reaches the same1525 body."""
    result=[]
    for level in("A","B"):
        blob=(Path(game)/f"data/script/pc/level{level}.jpc").read_bytes();p=ScriptPackage(blob);classes={c["name"]:c for c in p.classes};rows=[]
        for name,(kind,owner)in COMBAT_CALLERS[level].items():
            c=classes[owner];chain=[];resolved=None
            for _ in range(len(p.classes)):
                chain.append(c["name"])
                found=[m for m in p.methods[c["first_method"]:c["first_method"]+c["method_count"]]
                       if m["name"]=="processVFXAction"and m["descriptor"]=="(IIIII)Z"]
                if found:resolved=found[0]["index"];break
                if c["super_index"]<0:break
                c=p.classes[c["super_index"]]
            if resolved!=1525:raise ValueError("NPC20 has an unverified override")
            cases=[]
            for hp,rng in((100,0),(50,0),(50,99),(0,0)):
                calls=[]
                def random(bound):calls.append(bound);return rng
                vm=Interpreter(p,natives={("ed/base/External","random","(I)I"):random})
                npc=Object(classes[owner]["index"],{5:0,6:kind,13:hp});vm.statics[53]=Array('L',[npc]);vm.statics[54]=Array('I',[0]*40)
                consumed=vm.invoke(648,args=(kind,0,20,0,0,0,0,0,0,0,0))
                if consumed!=int(hp>=100 or rng<hp)or calls!=([]if hp>=100 else[100])or not any(m==1525 for m,pc in vm.trace):
                    raise ValueError("NPC20 original derived dispatch/RNG changed")
                cases.append({"health_fixture":hp,"rng_fixture":rng,"consumed":bool(consumed),"random_bounds":calls})
            rows.append({"actor":name,"native_type":kind,"script_class":owner,"class_chain":chain,"resolved_method":resolved,"cases":cases})
        m=p.methods[1525];code=blob[56+m["code_offset"]:56+m["code_offset"]+m["code_length"]]
        result.append({"level":level,"sha256":hashlib.sha256(blob).hexdigest(),"method1525_sha256":hashlib.sha256(code).hexdigest(),"rows":rows})
    return result


def blood_events(game):
    from export_actor import ACTORS,LEVEL_B_ACTORS
    rows=[]
    for level,profile,names in(("A",ACTORS,{"hellbillyfat":11,"hellbillythin":12,"troopknife":7,"troopclaw":8}),
                               ("B",LEVEL_B_ACTORS,{"badash":19,"guildmember":23,"wizardboss":26,"netherpet":31})):
        for name,kind in names.items():
            d=profile[name]
            for event in npc_events(game,21,[(name,d["archive"],d["packs"])]):rows.append({"level":level,"native_type":kind,**event})
    return rows


def source_callback(game):
    result=[]
    for level in("A","B"):
        data=(Path(game)/f"data/script/pc/level{level}.jpc").read_bytes();package=ScriptPackage(data);vm=Interpreter(package)
        owner=next(c["index"]for c in package.classes if c["name"]=="GenericNpc");rows=[]
        for actor_type in(4,35,31,36,37):
            npc=Object(owner,{5:0,6:actor_type});vm.statics[53]=Array('L',[npc]);vm.statics[54]=Array('I',[0]*40)
            consumed=vm.invoke(648,args=(actor_type,0,17,0,0,0,0,0,0,0,0))
            if consumed!=int(actor_type in(31,36,37)):raise ValueError("Original NPC17 predicate changed")
            rows.append({"script_field6":actor_type,"consumed":bool(consumed)})
        methods=[]
        for index in(648,1525):
            m=package.methods[index];start=56+m["code_offset"]
            methods.append({**m,"file_offset":start,"sha256":hashlib.sha256(data[start:start+m["code_length"]]).hexdigest()})
        result.append({"level":level,"sha256":hashlib.sha256(data).hexdigest(),"methods":methods,"cases":rows})
    return result


def combat_callback(game):
    """Execute the original NPC20 consume predicate, including shared RNG use."""
    results=[]
    for level in("A","B"):
        blob=(Path(game)/f"data/script/pc/level{level}.jpc").read_bytes();package=ScriptPackage(blob)
        owner=next(c["index"]for c in package.classes if c["name"]=="GenericNpc");rows=[]
        for hp,rng in((100,0),(99,0),(99,98),(99,99),(50,49),(50,50),(1,0),(1,1),(0,0)):
            calls=[]
            def random(bound):
                calls.append(bound);return rng
            vm=Interpreter(package,natives={("ed/base/External","random","(I)I"):random})
            npc=Object(owner,{5:0,6:4,13:hp});vm.statics[53]=Array('L',[npc]);vm.statics[54]=Array('I',[0]*40)
            consumed=vm.invoke(648,args=(4,0,20,0,0,0,0,0,0,0,0))
            if consumed!=int(hp>=100 or rng<hp)or calls!=([]if hp>=100 else[100])or npc.fields[13]!=hp:
                raise ValueError("Original NPC20 HP/RNG predicate changed")
            rows.append({"health_fixture":hp,"rng_fixture":rng,"consumed":bool(consumed),"random_bounds":calls})
        results.append({"level":level,"sha256":hashlib.sha256(blob).hexdigest(),"cases":rows})
    return results


def encode_npc_effects(definitions):
    data=bytearray(struct.pack("<8sI",b"EDVFXN1\0",len(definitions)))
    for d in definitions:
        if d["kind"]not in(16,17,21):raise ValueError("Unverified NPC VFX definition")
        data.extend(encode_hit_effects([d])[12:]);data.extend(struct.pack("<2I",d["random_child"],d["random_child_chance"]))
        if d["kind"]==16:
            data.extend(struct.pack("<I3fI",d["jitter_min_age"],*d["position_jitter"],len(d["children"])))
            for child in d["children"]:data.extend(struct.pack("<4B",child["flags"],child["kind"],child["chance"],0))
    return bytes(data)


def npc_goldens(game):
    from ed_vfx import SourceRandom,_f32 as f
    from ed_vfx_emitters import parse_emitter_effects
    from ed_vfx_ranged import parse_ranged_effects
    from ed_vfx_variants import parse_variant_effects
    exe=(Path(game)/"EvilDead.exe").read_bytes();definition=parse_npc_effects(exe)[0]
    definitions=[parse_emitter_effects(exe)[0],definition,parse_ranged_effects(exe)[1],parse_variant_effects(exe)["definitions"][1]]
    random=SourceRandom();reference=ReferenceHits(definitions,random);events=npc_events(game)
    requests=[(0,events[0]),(3,next(e for e in events if e["actor"]=="headlessskeleton"))]
    data=bytearray(struct.pack("<8s2I",b"EDVFXNG1",140,len(requests)))
    for when,e in requests:data.extend(struct.pack("<I",when)+bytes.fromhex(e["raw"]))
    for tick in range(140):
        for when,e in requests:
            if when==tick:
                a=struct.unpack_from("<3f",bytes.fromhex(e["raw"]),8);reference.spawn(17,[f(1.25-a[0]),f(.75+a[1]),f(-2+a[2])])
        if tick:reference.update()
        particles=reference.flat();data.extend(struct.pack("<7I64I",tick,len(particles),random.seed,random.index,int(random.ready),reference.coarse_calls,reference.floor_calls,*random.table))
        for p in particles:data.extend(struct.pack("<IQ7fiI4B3B?",p["kind"],p["serial"],*p["position"],*p["velocity"],p["size"],p["age"],p["tile"],*p["color"],*p["rotation"],p["stopped"]))
    return bytes(data)


def blood_goldens(game):
    from ed_vfx import SourceRandom,_f32 as f
    game=Path(game);exe=(game/"EvilDead.exe").read_bytes();definitions=[parse_blood_effect(exe),parse_hit_effects(exe)[1]]
    events=blood_events(game);requests=[(0,events[0]),(3,next(e for e in events if e["actor"]=="hellbillythin"))]
    random=SourceRandom();reference=ReferenceHits(definitions,random);data=bytearray(struct.pack("<8s2I",b"EDVFXBG1",320,len(requests)))
    for when,e in requests:data.extend(struct.pack("<I",when)+bytes.fromhex(e["raw"]))
    for tick in range(320):
        for when,e in requests:
            if when==tick:
                a,b=struct.unpack_from("<3f",bytes.fromhex(e["raw"]),8),struct.unpack_from("<3f",bytes.fromhex(e["raw"]),20)
                first=[f(1.25-a[0]),f(1.75+a[1]),f(-2+a[2])];second=[f(1.25-b[0]),f(1.75+b[1]),f(-2+b[2])]
                reference.spawn(21,first,second)
        if tick:reference.update()
        particles=reference.flat();data.extend(struct.pack("<7I64I",tick,len(particles),random.seed,random.index,int(random.ready),reference.coarse_calls,reference.floor_calls,*random.table))
        for p in particles:data.extend(struct.pack("<IQ7fiI4B3B?",p["kind"],p["serial"],*p["position"],*p["velocity"],p["size"],p["age"],p["tile"],*p["color"],*p["rotation"],p["stopped"]))
    return bytes(data)


def sixteen_goldens(game):
    from ed_vfx import SourceRandom,_f32 as f,parse_effects
    from ed_vfx_ranged import parse_ranged_effects
    game=Path(game);exe=(game/"EvilDead.exe").read_bytes();blank=parse_hit_effects(exe)[1]
    dust={**blank,**parse_effects(exe)[0],"dispersion":[0]*3,"direction_scale":0,"collision":0,"child_death":0,"rotation_step":[0]*3,
          "count_min":1,"count_max":1,"frame_mode":0,"frame_delay":0}
    definitions=[dust,parse_sixteen_effect(exe),parse_ranged_effects(exe)[1]]
    events=sixteen_events(game);requests=[(0,events[0]),(3,next(e for e in events if e["actor"]=="matriarchboss"))]
    random=SourceRandom();reference=ReferenceHits(definitions,random);data=bytearray(struct.pack("<8s2I",b"EDVFX16G",220,len(requests)))
    for when,e in requests:data.extend(struct.pack("<I",when)+bytes.fromhex(e["raw"]))
    for tick in range(220):
        for when,e in requests:
            if when==tick:
                a,b=struct.unpack_from("<3f",bytes.fromhex(e["raw"]),8),struct.unpack_from("<3f",bytes.fromhex(e["raw"]),20)
                first=[f(1.25-a[0]),f(1.75+a[1]),f(-2+a[2])];second=[f(1.25-b[0]),f(1.75+b[1]),f(-2+b[2])]
                reference.spawn(16,first,second)
        if tick:reference.update()
        particles=reference.flat();data.extend(struct.pack("<7I64I",tick,len(particles),random.seed,random.index,int(random.ready),reference.coarse_calls,reference.floor_calls,*random.table))
        for p in particles:data.extend(struct.pack("<IQ7fiI4B3B?",p["kind"],p["serial"],*p["position"],*p["velocity"],p["size"],p["age"],p["tile"],*p["color"],*p["rotation"],p["stopped"]))
    return bytes(data)


def export_npc_effects(game,output):
    """Export verified16/17/21 with original9/18/22/32 dependencies."""
    import os
    import tempfile
    game,output=Path(game).resolve(),Path(output).resolve()
    if output==game or game in output.parents:raise ValueError("NPC VFX export must stay outside originals")
    target=output/"vfx"
    if output not in target.resolve().parents:raise ValueError("NPC VFX output escapes generated assets")
    exe=(game/"EvilDead.exe").read_bytes();definitions=parse_npc_effects(exe)+[parse_blood_effect(exe),parse_sixteen_effect(exe)]
    files={"npc-effects.bin":encode_npc_effects(definitions),"npc-goldens.bin":npc_goldens(game),"blood-goldens.bin":blood_goldens(game),"sixteen-goldens.bin":sixteen_goldens(game)}
    target.mkdir(parents=True,exist_ok=True)
    for name,data in files.items():
        path=target/name
        if output not in path.resolve().parents:raise ValueError("NPC VFX output escapes generated assets")
        with tempfile.NamedTemporaryFile('wb',dir=target,delete=False)as stream:stream.write(data);temporary=stream.name
        try:os.replace(temporary,path)
        finally:
            if os.path.exists(temporary):os.unlink(temporary)
    return {"definitions":definitions,"source_sha256":hashlib.sha256(exe).hexdigest(),"events":npc_events(game),
        "callbacks":source_callback(game),"blood_events":blood_events(game),"sixteen_events":sixteen_events(game),"combat20_callbacks":combat_callback(game),"dependency":"Original9/18/22/32 from base/hit/ranged/variant definitions; no substitute or alias.",
        "files":{name:{"bytes":len(data),"sha256":hashlib.sha256(data).hexdigest()}for name,data in files.items()},
        "rotation17":"Modifier33 stores wrapped byte rotations0/+16/0; flatXZ renderer426B80 deliberately ignores them.",
        "emission17":"Immediate1, no emission RNG. Independent random32 chance75 then lifetime15; shared cached RNG order retained."}
