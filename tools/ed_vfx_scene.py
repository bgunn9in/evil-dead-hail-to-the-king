"""Exact JPC rock/fireball calls and original BG12 effect-table geometry."""
from pathlib import Path
import struct

from ed_scripts import ScriptPackage
from ed_vm import Interpreter,Object


def script_requests(game):
    result={}
    for level,index,owner,name in(("A",1870,"SceneAC05","dropRockOnAsh"),("B",1713,"Cthulhu","doAttackFireBall")):
        package=ScriptPackage((Path(game)/f"data/script/pc/level{level}.jpc").read_bytes());vm=Interpreter(package)
        requests=[];messages=[]
        for native in("vfxSpawnActorOffset","vfxSpawnActorJointRnd"):
            vm.natives[("ed/base/External",native,"(IIIIIII)V")]=lambda *args,native=native:requests.append((native,list(args)))
        vm.natives[("ed/base/External","sendActorMessage","(IIIIIIII)V")]=lambda *args:messages.append(list(args))
        ash=Object(vm.class_index("Ash"),{5:0,6:0});vm.statics[310]=ash
        receiver=Object(vm.class_index(owner),{1355:ash}if level=="A"else{5:0,6:33})
        vm.invoke(index,receiver,()if level=="A"else(0,));result[name]={"requests":requests,"messages":messages}
    return result


def scene_geometry_goldens(game,source):
    from ed_vfx import SourceRandom
    from ed_vfx_emitters import parse_emitter_effects
    from ed_vfx_tree import parse_tree_effects
    from ed_vfx_variants import ReferenceVariants,variant_geometry
    exe=(Path(game)/"EvilDead.exe").read_bytes();definitions=[parse_tree_effects(exe)["definitions"][1],parse_emitter_effects(exe)[1]]
    cases=[(kind,mode,age)for kind,mode in((25,11),(25,12),(24,6))for age in(0,1,8,16)]
    data=bytearray(struct.pack("<8sI",b"EDVFXSG2",len(cases)))
    for kind,mode,age in cases:
        reference=ReferenceVariants(definitions,SourceRandom());reference.spawn(kind,[1.25,.75,-2.],mode=mode)
        for _ in range(age):reference.update()
        geometry=variant_geometry(reference,source);data.extend(struct.pack("<4I",kind,mode,age,len(geometry)))
        for tint,vertices in geometry:
            data.extend(struct.pack("<4fI",*tint,len(vertices)))
            for vertex in vertices:data.extend(struct.pack("<5f",*vertex))
    return bytes(data)
