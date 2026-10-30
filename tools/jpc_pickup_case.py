"""Execute AK01's original vitamin pickup through JPC, including inventory logic.

The fixture supplies a headless actor/GUI registry and a source-sized inventory.
Pickup predicates, capacity/stacking, scene callback and mission flag execute from
the user's JPC. It does not replace Ash.pickupItem with a host implementation.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path

from ed_assets import parse_archive
from ed_camera import parse_scene
from ed_collision import parse_collision
from ed_doors import _interaction_zone
from ed_scripts import ScriptPackage, source_facing_allows
from ed_triggers import contains
from ed_vm import Array, Interpreter, Object, ScriptError
from export_scene import ROOT, source_floor_height


ITEM = 119
POSITION_XZ = (2.459147880077362, 8.356006050109864)


class PickupCase:
    def __init__(self, game):
        self.game = game
        script = (game / "data/script/pc/levelA.jpc").read_bytes()
        self.package = ScriptPackage(script)
        self.script_sha256 = hashlib.sha256(script).hexdigest()
        archive = (game / "data/ak01.pc").read_bytes()
        entry = next(e for e in parse_archive(archive) if e.resource_id == 0x0F0A0800)
        self.payload = archive[entry.offset:entry.offset + entry.size]
        self.scene_data = parse_scene(self.payload)
        self.collision = parse_collision(self.payload)
        self.position = [POSITION_XZ[0], source_floor_height(self.collision["floor"], *POSITION_XZ), POSITION_XZ[1]]
        marker = self.scene_data["markers"][33]
        self.yaw = math.atan2(marker[0] - self.position[0], marker[2] - self.position[2])
        self.events, self.gui_flags, self.gui_data = [], {}, {}
        self.gui_offsets = {200: (0, 0)}
        self.vm = Interpreter(self.package)

        def bind(name, callback):
            methods = [m for m in self.package.methods if m["class_name"] == "ed/base/External" and m["name"] == name]
            if len(methods) != 1:
                raise ValueError(f"Ambiguous native ABI: {name}")
            method = methods[0]
            self.vm.natives[(method["class_name"], method["name"], method["descriptor"])] = callback

        bind("actorInTrigger", self.in_trigger)
        bind("actorIsFacingMarker", self.facing_marker)
        bind("gameStateChange", lambda state: self.events.append({"native": "gameStateChange", "args": [state]}))
        bind("debugPrintString", lambda value: self.events.append({"native": "debugPrintString", "text": self.string(value)}))
        bind("debugPrintInt", lambda value: self.events.append({"native": "debugPrintInt", "args": [value]}))
        bind("guiSysAddFlag", self.add_gui_flag)
        bind("guiItemSetDataID", self.set_gui_data)
        bind("guiSysAtOffset", lambda handle, x, y: int(self.gui_offsets[handle] == (x, y)))

        self.ash = Object(self.vm.class_index("Ash"), {5: 0, 6: 0})
        self.scene = Object(self.vm.class_index("SceneAK01"))
        level = Object(self.vm.class_index("ed/base/EDLevel"), {38: self.scene})
        item_gui = Object(self.vm.class_index("InvItem"))
        textbox = Object(self.vm.class_index("GameTextBox"), {835: Array("I", [100]), 836: 200})
        self.vm.statics.update({310: self.ash, 396: level, 868: item_gui, 851: textbox})
        self.vm.initialize("MissionCommander")
        self.vm.initialize("SpawnCommander")
        self.inventory = Object(self.vm.class_index("Inventory"))
        # Ash.initialize659@40..53 passes slots12, accepted mask80, excluded258.
        self.vm.invoke(1299, self.inventory, (self.ash, 12, 80, 258))
        self.inventory.fields[1000] = item_gui
        self.ash.fields[99] = self.inventory
        self.item_gui = item_gui

    def set_scene(self, name, position_xz=None, facing_marker=None):
        """Change only the native-query/script context; no invented scene startup.

        New query poses keep native actor-local Y=0. These predicates ignore Y;
        deriving a destination BV height belongs to the scene/physics binder.
        """
        if not (len(name) == 4 and all("a" <= c <= "z" for c in name[:2]) and name[2:].isdigit()):
            raise ValueError("Invalid source scene name")
        blob = (self.game / f"data/{name}.pc").read_bytes()
        entries = [entry for entry in parse_archive(blob) if entry.resource_id >> 24 == 15]
        if len(entries) != 1:
            raise ValueError("Expected one source scene resource")
        entry = entries[0]
        self.payload = blob[entry.offset:entry.offset + entry.size]
        self.scene_data = parse_scene(self.payload)
        self.collision = None
        self.scene = Object(self.vm.class_index("Scene" + name.upper()))
        self.vm.statics[396].fields[38] = self.scene
        if position_xz is not None:
            self.position = [position_xz[0], 0, position_xz[1]]
        if facing_marker is not None:
            marker = self.scene_data["markers"][facing_marker]
            self.yaw = math.atan2(marker[0] - self.position[0], marker[2] - self.position[2])

    @staticmethod
    def string(value):
        if not isinstance(value, Object) or not isinstance(value.fields.get(0), Array):
            raise ScriptError("Native debug string is not a decoded source literal")
        start, count = value.fields[1], value.fields[2]
        return bytes(value.fields[0].values[start:start + count]).split(b"\0", 1)[0].decode("utf-8")

    def actor(self, actor_type, actor_index):
        if (actor_type, actor_index) != (self.ash.fields[6], self.ash.fields[5]):
            raise ScriptError("Unknown native actor in pickup fixture")

    def in_trigger(self, actor_type, actor_index, index):
        self.actor(actor_type, actor_index)
        zone = _interaction_zone(self.payload, index, (0, 1, 2))
        return int(contains(zone, self.position))

    def facing_marker(self, actor_type, actor_index, index, angle):
        self.actor(actor_type, actor_index)
        marker = self.scene_data["markers"][index]
        delta = self.yaw - math.atan2(marker[0] - self.position[0], marker[2] - self.position[2])
        return int(source_facing_allows(delta, angle))

    def add_gui_flag(self, handle, flag):
        self.gui_flags[handle] = self.gui_flags.get(handle, 0) | flag

    def set_gui_data(self, handle, asset):
        self.gui_data[handle] = asset

    def interact(self):
        owner = self.package.classes[self.scene.class_index]["name"]
        return self.vm.invoke(self.vm.method_index(owner, "allowUse", "(LAsh;I)I"), self.scene, (self.ash, 0))

    def items(self):
        return [{"slot": slot.fields[34], "item": slot.fields[33], "quantity": slot.fields[36], "flags": slot.fields[35]}
                for slot in self.inventory.fields[996].values if slot.fields[32] == 0]

    def fill_inventory(self):
        # Distinct source-defined inventory objects exercise actual add/stack/slot methods.
        for item in (3, 4, 5, 6, 7, 8, 9, 11, 15, 24, 29, 30):
            result = self.vm.invoke(1310, self.inventory, (item, 1))
            if result == -1:
                raise ValueError("Capacity fixture could not populate the source inventory")


def run_case(game):
    case = PickupCase(game)
    action = case.interact()
    methods = sorted({method for method, _ in case.vm.trace})
    steps = case.vm.steps
    item = case.items()
    if action != 116 or item != [{"slot": 0, "item": ITEM, "quantity": 1, "flags": 60}] or case.vm.statics[418] != 1:
        raise ValueError("Original pickup did not update inventory/action/mission state")
    if case.events != [{"native": "gameStateChange", "args": [2]}] or case.item_gui.fields[949] != 1:
        raise ValueError("Original pickup callback/UI request was not executed")
    if case.interact() != 0 or case.items() != item or case.gui_data.get(100) != 0x318A0804:
        raise ValueError("Collected pickup did not follow the original empty-location branch")
    full = PickupCase(game)
    full.fill_inventory()
    before = full.items()
    if full.interact() != 0 or full.items() != before or full.vm.statics[418] != 0:
        raise ValueError("Full inventory incorrectly consumed the scene pickup")
    if not any(e.get("text") == "Ash wasn't able to pickup the item" for e in full.events):
        raise ValueError("Full inventory did not execute the source failure diagnostic")
    return {"source_script_sha256": case.script_sha256, "scene": "ak01", "item": ITEM,
            "name_resource": "0x11990B24", "description_resource": "0x31990B24",
            "source_trigger": 76, "source_marker": 33, "position_world_xyz": case.position,
            "facing_yaw": case.yaw, "action": action, "inventory": item, "mission_field418": 1,
            "native_requests": case.events, "executed_methods_on_pickup": methods, "pickup_steps": steps,
            "repeat_empty_location_verified": True, "capacity_failure_verified": True,
            "limits": "Headless actor/GUI fixture; source script executes inventory logic. No rendering, native FSM116 or full scene startup."}


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
