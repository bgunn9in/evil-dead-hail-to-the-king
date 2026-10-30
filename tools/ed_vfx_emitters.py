"""Original powered-chainsaw smoke0 and bonus-birth ring25, native521/553."""
import struct
import math

from ed_actions import _va_reader
from ed_vfx_hits import parse_hit_effects, encode_hit_effects, ReferenceHits


def parse_emitter_effects(exe):
    blank = parse_hit_effects(exe)[1]  # Also verifies the original executable fingerprint.
    read = _va_reader(exe)
    f = lambda va: struct.unpack("<f", read(va, 4))[0]
    u = lambda va: struct.unpack("<I", read(va, 4))[0]
    cap = lambda kind: struct.unpack("<i", read(0x46A724+kind*4, 4))[0]
    smoke = {**blank, "kind":0,"constructor":0x427E50,"cap":cap(0),"life":u(0x427F98+3),
             "color":[128]*4,"delta":[4,4,4,-4],"velocity":[0,f(0x427EE6+3),0],"size_min":f(0x427ECB+3),
             "size_max":f(0x427ED2+3),"growth":f(0x427ED9+3),"uv":[f(0x427F39+2),f(0x427F24+1),f(0x427F24+1),f(0x427F45+3)],
             "columns":2,"rows":3,"frame_mode":2,"frame_delay":4,"frame_initial":0,"count_min":u(0x411FD3+3),"count_max":u(0x411FDD+3),
             "required_mode":0,"ring":[0]*6,"ring_color":[0]*3,"ring_segments":0}
    ring_data=struct.unpack("<6f8B",read(0x46F078+3*32,32))
    ring = {**blank,"kind":25,"constructor":0x42A3C0,"cap":cap(25),"life":u(0x42A4A7+3),
            "color":[255]*4,"delta":[0]*4,"size_min":0,"size_max":0,"growth":0,"uv":[f(0x42A45B+2),0,f(0x42A464+3),f(0x42A46B+3)],
            "columns":1,"rows":1,"random_frame":0,"frame_initial":0,"required_mode":3,
            "ring":list(ring_data[:6]),"ring_color":list(ring_data[6:9]),"ring_segments":ring_data[12]}
    return [smoke,ring]


def encode_emitter_effects(definitions):
    data=bytearray(struct.pack("<8sI",b"EDVFXE1\0",len(definitions)))
    for d in definitions:
        data.extend(encode_hit_effects([d])[12:])
        data.extend(struct.pack("<2I6f4B",d["frame_initial"],d["required_mode"],*d["ring"],*d["ring_color"],d["ring_segments"]))
    return bytes(data)


def source_chainsaw_requests(game, consumer=None):
    from pathlib import Path
    from ed_scripts import ScriptPackage
    from ed_vm import Interpreter,Object
    vm=Interpreter(ScriptPackage((Path(game)/"data/script/pc/levelA.jpc").read_bytes()))
    saw=Object(vm.class_index("GenericChainsaw"))
    vm.invoke(745,saw)  # Original Weapon and GenericChainsaw constructors, no selected-instruction replay.
    saw.fields.update({139:3,157:1,162:90,815:100,819:1,159:30,160:30,822:Object(vm.class_index("Ash"),{5:0,6:0})})
    requests=[]
    def emit(*args):
        requests.append(args)
        if consumer:consumer(args)
    vm.natives[("ed/base/External","vfxSpawnActorJointRnd","(IIIIIII)V")]=emit
    vm.invoke(747,saw)
    return requests


def source_birth_requests(game, consumer=None, bonus=4):
    from pathlib import Path
    from ed_scripts import ScriptPackage
    from jpc_npc_death_case import NpcDeathCase
    case=NpcDeathCase(ScriptPackage((Path(game)/"data/script/pc/levelA.jpc").read_bytes()),health=100)
    case.vm.initialize("SceneCommander")
    case.enemy.fields.update({1269:1,1299:bonus,1276:10})
    requests=[]
    def emit(*args):
        requests.append(args)
        if consumer:consumer(args)
    case.vm.natives[("ed/base/External","vfxSpawnActorOffset","(IIIIIII)V")]=emit
    for name,signature in (("actorFaceActor","(IIII)V"),("actorSetVisible","(III)V")):
        case.vm.natives[("ed/base/External",name,signature)]=lambda *args:None
    # Only the native action is rejected; all of1628 and source920 activation accounting execute.
    if case.vm.invoke(1628,case.enemy)!=1 or case.vm.statics[753]!=1:raise ValueError("Original birth effect path changed")
    return requests


def emitter_goldens(game, definitions):
    from ed_vfx import SourceRandom, _f32 as f
    random=SourceRandom()
    oracle=ReferenceHits(definitions,random)
    joint=(1.0,1.0,2.0)
    offsets=[]
    def emit(args):
        kind,actor_type,index,bone,repeat,width,mode=args
        if (kind,actor_type,index,bone,mode)!=(0,0,0,10,0):raise ValueError("Original chainsaw VFX ABI changed")
        for _ in range(repeat):
            cm=[random.bound(width)-(width//2) for _ in range(3)]
            offsets.append(cm)
            # Joint fixture has identity model rotation, but retains the native MODEL-X reflection.
            point=[f(joint[0]-cm[0]*f(.01)),f(joint[1]+cm[1]*f(.01)),f(joint[2]+cm[2]*f(.01))]
            oracle.spawn(kind,point)
    requests=source_chainsaw_requests(game,emit)
    births=source_birth_requests(game,lambda args:oracle.spawn(args[0],[.25,0,.75]))
    data=bytearray(struct.pack("<8sI",b"EDVFXK1\0",len(requests)))
    for args in requests:data.extend(struct.pack("<7i",*args))
    data.extend(struct.pack("<I",len(births)))
    for args in births:data.extend(struct.pack("<7i",*args))
    data.extend(struct.pack("<3fI",*joint,len(offsets)))
    for offset in offsets:data.extend(struct.pack("<3i",*offset))
    data.extend(struct.pack("<I",40))
    for tick in range(40):
        if tick:oracle.update()
        data.extend(struct.pack("<5I64I",tick,len(oracle.flat()),random.seed,random.index,int(random.ready),*random.table))
        for p in oracle.flat():data.extend(struct.pack("<IQ7fiI4B3BB",p["kind"],p["serial"],*p["position"],*p["velocity"],p["size"],p["age"],p["tile"],*p["color"],*p["rotation"],int(p["stopped"])))
    data.extend(struct.pack("<I",4))
    for age in (0,1,8,16):
        tint,vertices=ring_geometry(definitions[1],[.25,0,.75],age)
        data.extend(struct.pack("<I4fI",age,*tint,len(vertices)))
        for vertex in vertices:data.extend(struct.pack("<5f",*vertex))
    return bytes(data)


def ring_geometry(definition,point,age):
    from ed_vfx import _f32 as f
    r0,dr,theta0,dtheta,h0,dh=definition["ring"]
    radius=f(r0+age*dr*.0625)
    amplitude=f(h0+age*dh*.0625)
    angle=theta0+age*dtheta*.0625
    outer=f(radius+math.cos(angle)*amplitude)
    height=f(math.sin(angle)*amplitude)
    tint=[max(0,c-((c*age)>>4))/255 for c in definition["ring_color"]]
    tint.append((((0xFF000000-((age<<28)&0xFFFFFFFF))&0xFFFFFFFF)>>24)/255)
    previous_inner=[point[0],point[1],f(point[2]+radius)]
    previous_outer=[point[0],f(point[1]+height),f(point[2]+outer)]
    result=[]
    for segment in range(1,definition["ring_segments"]+1):
        theta=0 if segment==definition["ring_segments"] else segment*f(2*math.pi)/definition["ring_segments"]
        sine=f(math.sin(theta));cosine=math.cos(theta)
        inner=[f(point[0]+sine*radius),point[1],f(point[2]+cosine*radius)]
        edge=[f(point[0]+sine*outer),f(point[1]+height),f(point[2]+cosine*outer)]
        u0,v0,u1,v1=definition["uv"]
        quad=[[*previous_inner,u0,v1],[*inner,u1,v1],[*edge,u1,v0],[*previous_outer,u0,v0]]
        result.extend(quad[i] for i in (0,1,2,0,2,3))
        previous_inner,previous_outer=inner,edge
    return tint,result
