"""Original AL03 starter-cord pickup, AH01 item use and delayed AH02 passage."""

import argparse
import json
from pathlib import Path

from ed_assets import parse_archive
from ed_portals import active_portals, parse_portals
from ed_vm import Object
from jpc_conditional_door_case import ENGINE_ABI
from jpc_pickup_case import PickupCase


ROOT = Path(__file__).resolve().parents[1]
CORD = 29
PICKUP_XZ = (-28.650100708007812, 78.38780212402344)
USE_XZ = (-16.299055576324463, 1.064685881137848)


class CordDoorCase(PickupCase):
    """Reuse original inventory execution; host services never decide item use."""
    def __init__(self, game):
        super().__init__(game)
        self.voice_done = True
        self.engine_calls = []
        for owner in ("MissionCommander", "Ash", "GenericScene", "ObjDB", "PoopCommander"):
            self.vm.initialize(owner)
        self.vm.invoke(843)  # Original ObjDB.initialize creates indexed objects and usage state.
        for field, slots, accepted, excluded, gui in ((98, 5, 2, 256, "InvWeapon"),
                                                    (100, 14, 256, 66, "InvChest")):
            inventory = Object(self.vm.class_index("Inventory"))
            self.vm.invoke(1299, inventory, (self.ash, slots, accepted, excluded))
            inventory.fields[1000] = Object(self.vm.class_index(gui))
            self.ash.fields[field] = inventory
        archive = (game / "data/ah01.pc").read_bytes()
        entry = next(e for e in parse_archive(archive) if e.resource_id >> 24 == 15)
        self.portal_scene = parse_portals(archive[entry.offset:entry.offset + entry.size], 0x61680100)
        self.portal_enabled = {p["index"]: True for p in self.portal_scene["portals"]}

        def bind(name, descriptor):
            def native(*args):
                values = [self.string(value) if isinstance(value, Object) else value for value in args]
                self.engine_calls.append({"native": name, "args": values})
                if name == "scenePortalEnable":
                    portal, enabled = args
                    if portal not in self.portal_enabled or enabled not in (0, 1):
                        raise ValueError("Invalid original passage state")
                    self.portal_enabled[portal] = bool(enabled)
                if descriptor.endswith("V"):
                    return None
                if name == "randomRange":
                    return min(args)  # Deterministic ambient-audio wait; unrelated to passage conditions.
                return int(self.voice_done) if name == "sndMgrIsVoiceDone" else 0
            return native

        extra = (("debugPrintStringInt", "(ILjava/lang/String;)V"),
                 ("sndMgrPlaySoundVol", "(II)I"), ("sceneTriggerEnable", "(II)V"),
                 ("sndMgrIsVoiceDone", "(I)I"), ("randomRange", "(II)I"))
        for name, descriptor in (*ENGINE_ABI, *extra):
            self.vm.natives[("ed/base/External", name, descriptor)] = bind(name, descriptor)

    def activate(self, name, position, marker=None):
        self.set_scene(name, position, marker)
        owner = "Scene" + name.upper()
        self.vm.invoke(self.vm.method_index(owner, "<init>", "()V"), self.scene)
        self.vm.invoke(self.vm.method_index(owner, "activateScene", "(Z)V"), self.scene, (0,))

    def pickup_cord(self):
        self.activate("al03", PICKUP_XZ)
        return self.interact()

    def enter_machinery(self):
        self.activate("ah01", USE_XZ, 23)

    def apply_cord(self):
        return self.vm.invoke(669, self.ash, (CORD,))

    def sound_tick(self):
        self.vm.invoke(1672)

    def passage_ready(self):
        hits = active_portals(self.portal_scene, self.position,
                              [index for index, enabled in self.portal_enabled.items() if enabled])
        return any(portal["index"] == 3 for portal in hits)


def run_case(game: Path) -> dict:
    case = CordDoorCase(game)
    if (case.vm.statics[432], case.vm.statics[435]) != (0, 0):
        raise ValueError("Original mission initialization changed")
    pickup_action = case.pickup_cord()
    acquired = case.items()
    if pickup_action != 115 or acquired != [{"slot": 0, "item": CORD, "quantity": 1, "flags": 52}] or case.vm.statics[432] != 1:
        raise ValueError("Original cord pickup did not update inventory and location state")
    pickup_methods, pickup_steps = sorted({method for method, _ in case.vm.trace}), case.vm.steps
    case.enter_machinery()
    if case.passage_ready():
        raise ValueError("Original locked passage started enabled")
    result = case.apply_cord()
    use_methods, use_steps = sorted({method for method, _ in case.vm.trace}), case.vm.steps
    if result != 1 or case.items() or case.vm.statics[435] != 1 or case.vm.statics[1357] != 3:
        raise ValueError("Original item-use chain did not consume cord and request passage3")
    for tick in range(30):
        case.sound_tick()
        if case.passage_ready() != (tick == 29):
            raise ValueError("Original delayed passage did not honor the30-tick voice-complete boundary")
    if case.apply_cord() != 0:
        raise ValueError("Consumed cord could be used again")
    return {"script_sha256": case.script_sha256, "item": CORD, "source_label": "ТРОС",
            "pickup_scene": "al03", "pickup_trigger": 95, "pickup_position_xz": list(PICKUP_XZ),
            "pickup_action": pickup_action, "acquired_inventory": acquired,
            "pickup_methods": pickup_methods, "pickup_steps": pickup_steps,
            "use_scene": "ah01", "use_trigger": 50, "facing_marker": 23,
            "use_position_xz": list(USE_XZ), "use_yaw": case.yaw,
            "use_result": result, "use_methods": use_methods, "use_steps": use_steps,
            "remaining_inventory": case.items(), "mission_field432": case.vm.statics[432],
            "mission_field435": case.vm.statics[435], "source_scene_countdown1616": case.scene.fields[1616],
            "passage": {"portal": 3, "enabled_after_sound_ticks": 30, "destination": "ah02"},
            "limits": "Original scene initialization, inventory and item-use scripts execute. Engine audio/light/object/GUI services are headless fixtures; native FSM115 and AH02 rendering are not executed."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    args = parser.parse_args()
    try:
        print(json.dumps(run_case(args.game), indent=2))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
