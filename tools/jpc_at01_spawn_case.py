"""Bounded original AT01 spawn configuration/restore proof, without native actors."""

import argparse
import hashlib
import json
from pathlib import Path

from ed_vm import Array, Object
from jpc_pickup_case import PickupCase


def run_case(game, ticks=100):
    if not 0 <= ticks <= 10000:
        raise ValueError("Expected a bounded source tick count")
    case = PickupCase(Path(game))
    vm = case.vm
    clock, presentation = [0], []
    vm.natives[("ed/base/External", "getMilliseconds", "()I")] = lambda: clock[0]
    vm.natives[("ed/base/External", "debugPrintStringInt", "(ILjava/lang/String;)V")] = (
        lambda integer, string: presentation.append({"native": "debugPrintStringInt", "integer": integer}))
    case.set_scene("at01")
    for owner in ("ed/base/EDBase", "LevelA", "PoopCommander"):
        vm.initialize(owner)
    #Exact fresh byte-array shape allocated by LevelA1778 PCs2..18. This fixture
    #does not execute full actor bootstrap or manufacture a mission/wave flag.
    vm.statics[1499].values = [Array("B", [0] * 342) for _ in range(32)]
    level = Object(vm.class_index("LevelA"), {38: case.scene})
    vm.statics[396] = level
    index = vm.invoke(779, args=(0x61740100,))
    vm.invoke(1784, level, (index,))
    fresh_methods = sorted({method for method, _ in vm.trace})
    fresh_flags = vm.statics[793].values[3]
    fresh_type = vm.statics[796].values[12]
    vm.invoke(1785, level, (index,))
    stream = bytes(value & 255 for value in vm.statics[1499].values[index].values)
    vm.invoke(1784, level, (index,))
    restored_flags = vm.statics[793].values[3]
    #The other original waves include ammo/mushroom producers with independent
    #environment requirements. Isolate wave3 through original987+2086, then
    #run the real932 guard. No actorCreate native is bound: any attempt throws.
    vm.invoke(987)
    vm.invoke(2086, case.scene, (3, 0))
    for tick in range(ticks):
        clock[0] = tick * 33
        vm.statics[393] = tick
        vm.invoke(932)
    return {"source_script_sha256": case.script_sha256, "scene_index": index,
            "fresh_methods": fresh_methods, "fresh_wave3_flags": fresh_flags,
            "restored_wave3_flags": restored_flags, "wave3_resource_slot": 12,
            "wave3_type": fresh_type, "active_bit": 0x10000,
            "isolated_source_ticks": ticks, "isolated_wave3_flags": vm.statics[793].values[3],
            "isolated_in_progress": vm.invoke(996, args=(3,)),
            "saved_source_stream_size": len(stream), "saved_source_stream_sha256": hashlib.sha256(stream).hexdigest(),
            "presentation_requests": presentation,
            "limits": "Executes original fresh spawnRestore, save/restore and isolated inactive-wave frame guard. Does not prove every possible campaign/save state or run the original EXE. Other waves are not replaced with host gameplay predicates."}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=Path(__file__).resolve().parents[1] / "game")
    parser.add_argument("--ticks", type=int, default=100)
    args = parser.parse_args()
    print(json.dumps(run_case(args.game, args.ticks), indent=2))
