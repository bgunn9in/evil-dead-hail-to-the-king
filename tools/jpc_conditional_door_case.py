"""Execute the original AK09 progression-controlled AL01/AL50 portal choice."""

import argparse
import json
from pathlib import Path

from ed_scripts import ScriptPackage
from ed_vm import Array, Interpreter, Object, ScriptError, int32


ROOT = Path(__file__).resolve().parents[1]
ENGINE_ABI = (
    ("sndMgrGetSNDID", "(I)I"), ("sndMgrPlaySoundVolLoop", "(III)I"),
    ("lightSetColor", "(IIIII)V"), ("lightSetEnable", "(II)V"),
    ("objSetup", "(IIIII)V"), ("objSetBRadius", "(II)V"), ("objFlagsSub", "(II)I"),
    ("objSetPositionMarker", "(III)V"), ("objSetOrientation", "(IIII)V"),
    ("scenePortalEnable", "(II)V"),
)


def describe_case(package: ScriptPackage) -> dict:
    lookup = Interpreter(package)
    method = lookup.method_index("SceneAK09", "activateScene", "(Z)V")
    instructions = package.disassemble(method)
    tail = instructions[-12:]
    expected = ["getstatic", "ifne", "bipush", "invokestatic", "bipush", "invokestatic", "return",
                "bipush", "invokestatic", "bipush", "invokestatic", "return"]
    if ([i["mnemonic"] for i in tail] != expected or tail[0].get("field_index") != 405
            or tail[1]["target"] != tail[7]["offset"]
            or [tail[i]["operands"][0] for i in (2, 4, 7, 9)] != [11, 12, 11, 12]
            or [i["offset"] for i in instructions if i["opcode"] == 0xB1] != [tail[6]["offset"], tail[11]["offset"]]):
        raise ValueError("AK09 conditional portal block no longer matches the verified source")
    for index, name in ((3, "portalEnable"), (5, "portalDisable"), (8, "portalDisable"), (10, "portalEnable")):
        target = tail[index]["method"]
        if (target["class_name"], target["name"], target["descriptor"]) != ("ed/base/EDScene", name, "(I)V"):
            raise ValueError("AK09 conditional portal has a changed effect")
        wrapper = package.disassemble(target["index"])
        if ([i["mnemonic"] for i in wrapper] != ["iload_0", "iconst_1" if name == "portalEnable" else "iconst_0",
                                                "invokestatic", "return"]
                or tuple(wrapper[2]["method"][key] for key in ("class_name", "name", "descriptor"))
                != ("ed/base/External", "scenePortalEnable", "(II)V")):
            raise ValueError("Changed native portal wrapper")
    initialize = lookup.method_index("MissionCommander", "<clinit>", "()V")
    initial = package.disassemble(initialize)
    writes = [index for index, item in enumerate(initial) if item["opcode"] == 0xB3 and item["field_index"] == 405]
    if len(writes) != 1 or not writes[0] or initial[writes[0] - 1]["mnemonic"] != "iconst_0":
        raise ValueError("Unknown initial progression state")
    return {"method_index": method, "scene_class_index": lookup.class_index("SceneAK09"),
            "condition_field": 405, "condition_pc": tail[0]["offset"], "branch_pc": tail[1]["offset"],
            "zero_path_pcs": [item["offset"] for item in tail[:7]],
            "nonzero_path_pcs": [tail[0]["offset"], tail[1]["offset"], *[item["offset"] for item in tail[7:]]],
            "initializer_method": initialize, "initializer_write_pc": initial[writes[0]]["offset"],
            "initial_value": 0}


def make_case(package: ScriptPackage, progression_state=None):
    """Build a bounded engine fixture; execute all scene script setup unchanged.

    Audio/light/object engine calls are recorded without rendering or playback.
    Their integer results are opaque fixture handles0. The original field405
    predicate and all portal wrappers execute in the shared VM, without a mock.
    """
    case = describe_case(package)
    engine = {"portal_enabled": {index: True for index in range(13)}, "portal_operations": [], "engine_calls": []}

    def bind(name, descriptor):
        def native(*args):
            engine["engine_calls"].append({"name": name, "args": list(args)})
            if name == "scenePortalEnable":
                index, enabled = args
                if index not in engine["portal_enabled"] or enabled not in (0, 1):
                    raise ScriptError("Invalid source portal state")
                engine["portal_enabled"][index] = bool(enabled)
                engine["portal_operations"].append([index, enabled])
            return None if descriptor.endswith("V") else 0
        return native

    vm = Interpreter(package, natives={("ed/base/External", name, descriptor): bind(name, descriptor)
                                      for name, descriptor in ENGINE_ABI})
    initialized = []
    for owner in ("Main", "MissionCommander", "Ash", "GenericScene", "ObjDB"):
        vm.initialize(owner)
        initialized.append({"class": owner, "steps": vm.steps, "method_index": vm.trace[0][0] if vm.trace else None})
    engine["initial_field_value"] = vm.statics[case["condition_field"]]
    if progression_state is not None:
        vm.statics[case["condition_field"]] = int32(progression_state)
    engine["input_field_value"] = vm.statics[case["condition_field"]]
    engine["initialized_classes"] = initialized
    vm.statics[310] = Object(vm.class_index("Ash"))
    pool = vm.statics[700]
    if not isinstance(pool, Array) or pool.kind != "L" or len(pool.values) != 32:
        raise ValueError("Original ObjDB pool shape changed")
    # Fixture provides engine-created object references; allocation is not a door condition.
    pool.values[:] = [Object(vm.class_index("Obj")) for _ in pool.values]
    return vm, Object(case["scene_class_index"]), engine


def run_case(package: ScriptPackage, progression_state=None) -> dict:
    case = describe_case(package)
    vm, receiver, engine = make_case(package, progression_state)
    vm.invoke(case["method_index"], receiver, (0,))
    return {**engine, "method_index": case["method_index"], "condition_field": case["condition_field"],
            "steps": vm.steps, "executed_methods": sorted({method for method, _ in vm.trace}),
            "condition_trace": [pc for method, pc in vm.trace
                                if method == case["method_index"] and pc >= case["condition_pc"]],
            "final_field_value": vm.statics[case["condition_field"]]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--state", type=int, help="Restored source field405; omission uses the original class initializer")
    args = parser.parse_args()
    try:
        package = ScriptPackage((args.game / "data/script/pc/levelA.jpc").read_bytes())
        result = run_case(package, args.state)
        print(json.dumps({key: value for key, value in result.items() if key != "engine_calls"}, indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
