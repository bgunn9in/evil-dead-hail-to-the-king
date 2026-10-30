"""Execute Ash's original hit callback, binding only the native test environment.

The GUI handles below are fixture handles, not decoded game asset identifiers.
Health, damage, reaction selection and portrait changes run from levelA.jpc.
"""

import argparse
import json
from pathlib import Path

from ed_scripts import ScriptPackage
from ed_vm import Array, Interpreter, Object


class AshHitCase:
    def __init__(self, package, health=100, guard=0, damage_suppressed=0,
                 milliseconds=1234, transition_possible=True, at_home=True):
        self.calls = []
        self.transition_possible = transition_possible
        self.at_home = at_home
        self.gui_values = {}
        self.gui_colors = {}
        self.gui_flags = {7: 1}
        self.ash = Object(18, {5: 0, 6: 1, 7: -1, 8: -1, 9: 0, 10: 0, 11: 0,
                               13: health, 130: 0, 1259: -1, 1260: -1})
        self.face = Object(25, {379: self.ash, 380: 0, 835: Array("I", [11]),
                                836: 7, 837: 0, 840: 0, 841: 0})
        names = ("guiItemSetMayaVal", "guiItemSetColor", "guiSysAtOffset",
                 "guiSysAddFlag", "guiSysRemoveFlag", "random",
                 "stateTransitionIsPossible", "actorDoConsoleAction",
                 "vfxSpawnActorJoint", "sndMgrGetSNDID", "sndMgrPlaySoundVol")
        natives = {}
        for name in names:
            methods = [m for m in package.methods if m["class_name"] == "ed/base/External"
                       and m["name"] == name]
            if len(methods) != 1:
                raise ValueError(f"Expected one original External.{name}")
            method = methods[0]
            natives[(method["class_name"], name, method["descriptor"])] = self._native(name)
        self.vm = Interpreter(package, natives=natives, statics={
            53: Array("LGenericNpc;", [self.ash]), 54: Array("I", [0, 0]),
            60: damage_suppressed, 309: 0x616B3031, 394: guard,
            398: milliseconds, 849: self.face})
        self.vm.initialize("EventManager")
        self.hit_method = self.vm.method_index("Ash", "eventAttackHit", "(IIII)V")
        self.dispatch_method = self.vm.method_index("ActorDB", "processMessage", "(IIIIIIIIIII)I")
        self.death_method = self.vm.method_index("GenericNpc", "doReactionDeath", "()I")
        if self.hit_method != 703 or self.vm.class_index("Ash") != self.ash.class_index:
            raise ValueError("This fixture requires the decoded PC levelA field/method layout")

    def _native(self, name):
        def call(*args):
            self.calls.append((name, args))
            if name == "guiItemSetMayaVal":
                self.gui_values[args[:2]] = args[2]
            elif name == "guiItemSetColor":
                self.gui_colors[args[0]] = args[1:]
            elif name == "guiSysAddFlag":
                self.gui_flags[args[0]] = self.gui_flags.get(args[0], 0) | args[1]
            elif name == "guiSysRemoveFlag":
                self.gui_flags[args[0]] = self.gui_flags.get(args[0], 0) & ~args[1]
            elif name == "guiSysAtOffset":
                return int(self.at_home)
            elif name == "stateTransitionIsPossible":
                return int(self.transition_possible)
            elif name == "actorDoConsoleAction":
                # shortcut: this fixture rejects native actions; an accepted action needs the original event scheduler.
                return 0
            elif name == "random":
                return 0
            elif name == "sndMgrGetSNDID":
                return 17
            elif name == "sndMgrPlaySoundVol":
                return 23
        return call

    def hit(self, damage, attack_type=36, attacker_type=2, attacker_id=0):
        self.last_dispatch_result = self.vm.invoke(self.dispatch_method, args=(
            1, 0, 0, 0, attacker_type, attacker_id, attack_type, damage, 0, 0, 0))
        return self.ash.fields[13]

    def death_reaction(self):
        return self.vm.invoke(self.death_method, self.ash)


def run_case(package):
    case = AshHitCase(package, health=35)
    case.hit(12)
    first_steps = case.vm.steps
    first_trace = list(case.vm.trace)
    first = {"health": case.ash.fields[13], "portrait": case.gui_values[(11, 0)],
             "color": list(case.gui_colors[11]), "action_requested": case.ash.fields[8],
             "script_instructions": first_steps}
    case.hit(100)
    lethal_steps = case.vm.steps
    return {"entry": "ActorDB.processMessage(IIIIIIIIIII)I",
            "event": "Ash.eventAttackHit(IIII)V", "first_hit": first,
            "lethal_hit": {"health": case.ash.fields[13],
                           "death_reaction": case.death_reaction(),
                           "script_instructions": lethal_steps},
            "native_calls": [{"name": name, "args": list(args)} for name, args in case.calls],
            "first_hit_trace": first_trace,
            "limits": ["Native console actions are rejected, so this proves script reaction requests, not FSM animation or asynchronous completion.",
                       "The original native producer of ActorDB.processMessage has not yet been recovered."]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--script", type=Path, default=Path(__file__).resolve().parents[1] /
                        "game/data/script/pc/levelA.jpc")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = run_case(ScriptPackage(args.script.read_bytes()))
    encoded = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(encoded, encoding="utf-8")
    else:
        print(encoded, end="")
