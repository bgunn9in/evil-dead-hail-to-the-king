"""Bounded original LevelA activation/fresh/revisit probe, with strict native gaps.

This is not full actor/GUI startup. It retains the existing presentation registry
fixture, executes original scene constructors/activation and Spawn save/restore,
and leaves actor creation and unmodeled queries unbound. Unsupported means an
actual executed call failed, not that a literal actor ID appeared in bytecode.
"""

import argparse
import hashlib
import json
from pathlib import Path

from audit_scene_coverage import registry
from ed_vm import Array, Object, ScriptError
from jpc_scene_activation_case import ROOT, SceneActivationCase


class LevelASceneProbe(SceneActivationCase):
    def __init__(self, game):
        super().__init__(game)
        self.position = [0., 0., 0.]  # Native actorCreate's confirmed initial local position.
        self.clock = 0
        self.vm.natives[("ed/base/External", "getMilliseconds", "()I")] = lambda: self.clock
        # Source diagnostics are presentation only. All other unknown calls throw.
        self.vm.natives[("ed/base/External", "debugPrintStringInt", "(ILjava/lang/String;)V")] = (
            lambda integer, string: self._record("debugPrintStringInt", (integer, self.string(string))))
        for owner in ("ed/base/EDBase", "LevelA", "PoopCommander"):
            self.vm.initialize(owner)
        # Exact source LevelA.create1778@2..18 stream allocation, without actors.
        self.vm.statics[1499].values = [Array("B", [0] * 342) for _ in range(32)]

    def probe(self, name):
        result = {"scene": name, "script_sha256": self.script_sha256,
                  "native_actor_input": {"local_xyz": self.position.copy(), "flags": self.actor_flags},
                  "stages": [], "status": "incomplete"}

        def execute(stage, callback):
            try:
                value = callback()
            except (ScriptError, ValueError, KeyError, IndexError) as error:
                result["stages"].append({"stage": stage, "completed": False, "error": str(error),
                                         "last_source_location": list(self.vm.trace[-1]) if self.vm.trace else None})
                return False
            result["stages"].append({"stage": stage, "completed": True,
                                     "executed_methods": sorted({mid for mid, _ in self.vm.trace}),
                                     "steps": self.vm.steps})
            return value if value is not None else True

        first = execute("original constructor + activateScene(false), initial mission globals",
                        lambda: self.activate_arrival(name))
        if first is False:
            result["presentation_requests"] = self.activation_calls.copy()
            return result
        result["initial_activation"] = first
        level = Object(self.vm.class_index("LevelA"), {38: self.scene})
        self.vm.statics[396] = level
        self.vm.statics[309] = first["scene_id"]  # Native loaded-scene context; Game771 stores this ID.
        index = self.vm.invoke(779, args=(first["scene_id"],))
        result["scene_index"] = index
        if not 0 <= index < 32:
            raise ValueError("Original LevelA scene stream index exceeds its allocation")
        if execute("LevelA.sceneRestore1784 zero source room stream (fresh)",
                   lambda: self.vm.invoke(1784, level, (index,))) is False:
            result["presentation_requests"] = self.activation_calls.copy()
            return result
        flags = self.vm.statics[793].values.copy()
        result["fresh_waves"] = [{"index": i, "flags": flag, "active": bool(flag & 0x10000),
                                   "prototypes": self.vm.statics[796].values[i * 4:i * 4 + 4]}
                                  for i, flag in enumerate(flags)]
        if execute("LevelA.sceneSave1785", lambda: self.vm.invoke(1785, level, (index,))) is False:
            return result
        data = bytes(value & 255 for value in self.vm.statics[1499].values[index].values)
        result["saved_room_stream_sha256"] = hashlib.sha256(data).hexdigest()
        if execute("original constructor + activateScene(false), preserved mission globals (revisit)",
                   lambda: self.activate_arrival(name)) is False:
            return result
        level.fields[38] = self.scene
        self.vm.statics[396] = level
        self.vm.statics[309] = first["scene_id"]
        if execute("LevelA.sceneRestore1784 saved source room stream (revisit)",
                   lambda: self.vm.invoke(1784, level, (index,))) is False:
            return result
        result["status"] = "completed_bounded_scene_restore"
        result["revisit_wave_flags"] = self.vm.statics[793].values.copy()
        result["presentation_requests"] = self.activation_calls.copy()
        return result


def run_probe(game, scenes=None):
    if scenes is None:
        case = LevelASceneProbe(game)
        scenes = [item["scene"] for item in registry(case.package, "LevelA")]
    cases = [LevelASceneProbe(game).probe(name) for name in scenes]
    return {"format": "ED-LEVELA-SCENE-PROBE1", "cases": cases,
            "completed": [case["scene"] for case in cases if case["status"] == "completed_bounded_scene_restore"],
            "strict_gaps": [{"scene": case["scene"], **next(stage for stage in case["stages"] if not stage["completed"])}
                            for case in cases if case["status"] != "completed_bounded_scene_restore"],
            "limits": ["Original scripts execute; presentation calls are recorded, not actually played/rendered.",
                       "Fresh/revisit refer to original room stream paths, not activateScene(true) new-game cheats.",
                       "Actor creation, native query gaps and full Main/GUI bootstrap remain strict; this is not a playthrough proof."]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    data = json.dumps(run_probe(args.game), indent=2) + "\n"
    if args.output:
        args.output.write_text(data, encoding="utf-8")
    else:
        print(data, end="")
