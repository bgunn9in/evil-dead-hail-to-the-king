"""Original Deadite weapon event, Axe damage, AI death choice and native request."""

import argparse
import json
from pathlib import Path

from ed_scripts import ScriptPackage
from ed_vm import Array, Interpreter, Object, ScriptError


ROOT = Path(__file__).resolve().parents[1]
YIELD_MARKER = "Fixture reached original death-action event24 wait"


class NpcDeathCase:
    def __init__(self, package, health=12, half_damage=False, line_of_sight=True, visible=True, distance=100):
        self.calls = []
        self.line_of_sight, self.visible, self.distance = line_of_sight, visible, distance
        self.death_action = None
        self.vm = Interpreter(package)
        self.enemy = Object(self.vm.class_index("Deadite"))
        self.vm.invoke(self.vm.method_index("Deadite", "<init>", "()V"), self.enemy)
        self.constructor_methods = sorted({method for method, _ in self.vm.trace})
        factory = package.disassemble(self.vm.method_index("ActorDB", "create", "(IIII)LGenericNpc;"))
        positions = {item["offset"]: item for item in factory}
        self.actor_type = next(kind for item in factory for kind, target in item.get("cases", [])
                               if positions[target].get("class_name") == "Deadite")
        self.axe = Object(self.vm.class_index("Axe"))
        self.ash = Object(self.vm.class_index("Ash"), {5: 0, 6: 0, 102: self.axe})
        # Engine fixture: living target already follows Ash; HP is a test value, not original balance.
        self.enemy.fields.update({5: 0, 6: self.actor_type, 13: health, 1245: 0, 1263: 64,
                                  1271: int(half_damage), 1284: self.ash})
        self.enemy.fields[4].fields.update({21: 0, 22: 0, 23: 0})
        defaults = package.disassemble(self.vm.method_index("Deadite", "initialize", "()V"))
        threshold = next(defaults[index - 1]["operands"][0] for index, item in enumerate(defaults)
                         if item["opcode"] == 0xB5 and item["field_index"] == 1328)
        self.enemy.fields[1328] = threshold
        registry_offsets = [0] * (self.actor_type + 1)
        registry_offsets[self.actor_type] = 1
        self.vm.statics.update({53: Array("L", [self.ash, self.enemy]), 54: Array("I", registry_offsets),
                                310: self.ash})
        self.vm.initialize("EventManager")
        names = ("actorHaveLOSToActor", "sceneIsNPCInVisArea", "actorDistanceToActor", "threadSleepFrame",
                 "actorDoConsoleAction", "actorSetBRadius", "random", "vfxSpawnActorJoint")
        for name in names:
            methods = [method for method in package.methods
                       if method["class_name"] == "ed/base/External" and method["name"] == name]
            if len(methods) != 1:
                raise ValueError(f"Ambiguous native ABI {name}")
            method = methods[0]
            self.vm.natives[(method["class_name"], name, method["descriptor"])] = self.native(name)

    def native(self, name):
        def call(*args):
            self.calls.append({"name": name, "args": list(args)})
            if name == "actorHaveLOSToActor":
                return int(self.line_of_sight)
            if name == "sceneIsNPCInVisArea":
                return int(self.visible)
            if name == "actorDistanceToActor":
                return self.distance
            if name == "random":
                return 0
            if name == "actorDoConsoleAction":
                if args[-1] in (64, 65, 66, 67, 118, 119, 120, 121):
                    self.death_action = args[-1]
                    return 1
                return 0
            if name == "threadSleepFrame" and self.death_action is not None:
                # Stop at the real cooperative wait; no completion event or animation is invented.
                raise ScriptError(YIELD_MARKER)
        return call

    def hit(self, selector=-2):
        method = self.vm.method_index("ActorDB", "processMessage", "(IIIIIIIIIII)I")
        self.dispatch_result = self.vm.invoke(method, args=(self.actor_type, 0, 0, 0, 0, 0, 5, selector, 0, 0, 0))
        return self.enemy.fields[13]

    def think_to_death_request(self):
        method = self.vm.method_index("ActorDB", "think", "(II)V")
        try:
            self.vm.invoke(method, args=(self.actor_type, 0))
        except ScriptError as error:
            if YIELD_MARKER not in str(error) or self.death_action is None:
                raise
            self.yield_diagnostic = str(error)
            return self.death_action
        raise ValueError("NPC think unexpectedly returned without a native cooperative wait")


def run_case(package):
    case = NpcDeathCase(package)
    first = case.hit()
    first_methods = sorted({method for method, _ in case.vm.trace})
    lethal = case.hit()
    lethal_methods = sorted({method for method, _ in case.vm.trace})
    action = case.think_to_death_request()
    if (case.actor_type, first, lethal, case.enemy.fields[1263], action) != (4, 4, 0, 32, 64):
        raise ValueError("Original Deadite weapon/death path changed")
    return {"entry": "ActorDB.processMessage649, event5, primary-weapon selector-2",
            "actor_type": case.actor_type, "enemy_class": "Deadite", "weapon_class": "Axe",
            "fixture_initial_health": 12, "source_damage_per_hit": 8, "health_after_hits": [first, lethal],
            "constructor_methods": case.constructor_methods,
            "first_hit_methods": first_methods, "lethal_hit_methods": lethal_methods,
            "think_entry": "ActorDB.think651", "death_behavior": case.enemy.fields[1263],
            "native_death_action": action, "think_methods": sorted({method for method, _ in case.vm.trace}),
            "native_calls": case.calls, "yield_diagnostic": case.yield_diagnostic,
            "limits": "HP, visibility, contact distance and initial follow state are engine fixtures. Original dispatcher, damage table and AI execute. Stops at the original native frame yield awaiting event24; death animation/completion/loot are not executed."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--script", type=Path, default=ROOT / "game/data/script/pc/levelA.jpc")
    args = parser.parse_args()
    try:
        print(json.dumps(run_case(ScriptPackage(args.script.read_bytes())), indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
