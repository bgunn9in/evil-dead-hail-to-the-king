"""Execute registered original LevelA constructors and arrival activation."""

import argparse
import hashlib
import json
from pathlib import Path
import struct

from ed_vm import ScriptError, int32
from jpc_cord_door_case import CordDoorCase, ROOT
from audit_scene_coverage import registry
from ed_triggers import contains


SCENES = ("ak01", "ak09", "al01", "al03", "ah50", "ah51", "ah01", "ah02")
SUPPORTED_SCENES = (*SCENES, "ab50", "ac51", "al50")
# These engine services produce presentation requests, never passage predicates.
PRESENTATION_ABI = (
    ("objSetVisible", "(II)V"), ("sndMgrAddVoiceFlags", "(II)V"),
    ("sndMgrSetVoicePositionMarker", "(II)V"), ("sndMgrSetVoiceRadius", "(II)V"),
    ("moviePlay", "(II)V"),
    ("rumbleP1", "(III)V"), ("scrFxShake", "(I)V"),
    ("lightSetMode", "(II)V"),
    ("lightSetSpeed", "(II)V"),
    ("lightSetFrameSpeed", "(II)V"), ("lightSetPositionMarker", "(III)V"),
)


class SceneActivationCase(CordDoorCase):
    def __init__(self, game):
        super().__init__(game)
        self.activation_calls = []
        self.volume_enabled = {}
        self.active_name = None
        self.actor_flags = 1  # Explicit active Ash native-registry input, not a new-game bootstrap claim.
        self.object_properties = {}
        self.registered_scenes = {item["scene"] for item in registry(self.package, "LevelA")}
        for name, descriptor in PRESENTATION_ABI:
            self.vm.natives[("ed/base/External", name, descriptor)] = self._presentation(name)
        self.vm.natives[("ed/base/External", "scenePortalEnable", "(II)V")] = self._set_portal
        self.vm.natives[("ed/base/External", "sceneTriggerEnable", "(II)V")] = self._set_volume
        self.vm.natives[("ed/base/External", "actorInTrigger", "(III)I")] = self._in_raw_volume
        self.vm.natives[("ed/base/External", "actorFlagsSub", "(III)I")] = self._actor_flags_sub
        self.vm.natives[("ed/base/External", "actorSetPosition", "(IIIII)V")] = self._actor_position
        self.vm.natives[("ed/base/External", "objSetup", "(IIIII)V")] = self._object_setup
        self.vm.natives[("ed/base/External", "objSetPositionMarker", "(III)V")] = self._object_marker
        self.vm.natives[("ed/base/External", "objTranslatePosition", "(IIII)V")] = self._object_translate
        self.vm.natives[("ed/base/External", "objFlagsSub", "(II)I")] = self._object_flags_sub
        self.vm.natives[("ed/base/External", "audioPlay", "(II)V")] = self._presentation("audioPlay")

    @staticmethod
    def _float(value):
        return struct.unpack("<f", struct.pack("<f", value))[0]

    def _actor_position(self, actor_type, actor_index, x, y, z):
        self.actor(actor_type, actor_index)
        self.position = [self._float(value * self._float(.01)) for value in (x, y, z)]
        self._record("actorSetPosition", (actor_type, actor_index, x, y, z))

    def _actor_flags_sub(self, actor_type, actor_index, mask):
        self.actor(actor_type, actor_index)
        if self.actor_flags & mask:
            self.actor_flags ^= mask
        self._record("actorFlagsSub", (actor_type, actor_index, mask))
        return int32(self.actor_flags)

    def _in_raw_volume(self, actor_type, actor_index, index):
        self.actor(actor_type, actor_index)
        count, offset = struct.unpack_from("<II", self.payload, 8)
        if not 0 <= index < count:
            raise ScriptError("Source actorInTrigger index is out of range")
        start = offset + index * 72
        shape = self.payload[start + 1]
        if shape not in range(6):
            raise ScriptError("Source actorInTrigger shape is unverified")
        point_count = {0: 2, 1: 3, 2: 4, 3: 2, 4: 2, 5: 2}[shape]
        points = [list(point) for point in struct.iter_unpack("<3f", self.payload[start + 4:start + 4 + point_count * 12])]
        # Native4199A0 accepts every volume category and ignores active state.
        return int(contains({"shape": shape, "points": points}, self.position))

    def _object_setup(self, index, item, mesh, unused, texture):
        if not 0 <= index < 32:
            raise ScriptError("Source object slot is out of range")
        self.object_properties[index] = {"position": [0., 0., 0.], "flags": 0x0B, "marker": -1}
        self.engine_calls.append({"native": "objSetup", "args": [index, item, mesh, unused, texture]})

    def _object_marker(self, index, marker, mode):
        value = self.object_properties[index]
        position = list(self.scene_data["markers"][marker])
        if mode == 1:
            position[1] = 0
            value["flags"] |= 2
        else:
            value["flags"] &= ~2
        value.update(position=position, marker=marker)
        self.engine_calls.append({"native": "objSetPositionMarker", "args": [index, marker, mode]})

    def _object_translate(self, index, x, y, z):
        value = self.object_properties[index]
        delta = [self._float(amount * self._float(.01)) for amount in (x, y, z)]
        if any(delta):
            value["position"] = [self._float(a + b) for a, b in zip(value["position"], delta)]
            value["marker"] = -1
        self._record("objTranslatePosition", (index, x, y, z))

    def _object_flags_sub(self, index, mask):
        value = self.object_properties[index]
        if value["flags"] & mask:
            value["flags"] = ((value["flags"] ^ mask) + 0x8000) % 0x10000 - 0x8000
        self.engine_calls.append({"native": "objFlagsSub", "args": [index, mask]})
        return value["flags"]

    def _record(self, name, args):
        self.activation_calls.append({"native": name, "args": list(args),
                                      "caller": list(self.vm.trace[-1]) if self.vm.trace else None})

    def _presentation(self, name):
        def invoke(*args):
            self._record(name, args)
        return invoke

    def _set_portal(self, index, enabled):
        if index not in self.portal_enabled or enabled not in (0, 1):
            raise ScriptError("Invalid activation portal request")
        self._record("scenePortalEnable", (index, enabled))
        self.portal_enabled[index] = bool(enabled)

    def _set_volume(self, index, enabled):
        if index not in self.volume_enabled or enabled not in (0, 1):
            raise ScriptError("Invalid activation volume request")
        self._record("sceneTriggerEnable", (index, enabled))
        self.volume_enabled[index] = bool(enabled)

    def activate_arrival(self, name, restored_fields=None):
        """Use activateScene(false): arrivals must not execute new-game cheats.

        Scene loader415EF9 activates every volume before this JPC call. Portal
        load state is likewise enabled. Restored mission slots are input state,
        while every activation decision and resulting write executes original JPC.
        """
        if name not in self.registered_scenes:
            raise ValueError("Scene is outside the original LevelA factory")
        self.set_scene(name)
        self.active_name = name
        scene_id = (ord(name[0]) << 24) | (ord(name[1]) << 16) | (int(name[2:]) << 8)
        portal_count, portal_offset = struct.unpack_from("<II", self.payload, 0x28)
        if portal_offset + 72 * portal_count > len(self.payload):
            raise ValueError("Invalid source portal table")
        self.portal_enabled = {index: True for index in range(portal_count)}
        volume_count = struct.unpack_from("<I", self.payload, 8)[0]
        self.volume_enabled = {index: True for index in range(volume_count)}
        self.activation_calls.clear()
        self.engine_calls.clear()
        for field, value in (restored_fields or {}).items():
            if not 400 <= field < 700:
                raise ValueError("Expected an explicit mission field slot")
            self.vm.statics[field] = int32(value)
        owner = "Scene" + name.upper()
        constructor = self.vm.method_index(owner, "<init>", "()V")
        method = self.vm.method_index(owner, "activateScene", "(Z)V")
        self.vm.invoke(844)  # LevelA.endScene1782@65 releases the previous scene's object slots.
        self.vm.invoke(constructor, self.scene)
        before = {key: value for key, value in self.vm.statics.items() if isinstance(value, int)}
        self.vm.invoke(method, self.scene, (0,))
        method_data = self.package.methods[method]
        executed = set(self.vm.trace)
        native_abi = {}
        for index in {index for index, _ in executed}:
            for instruction in self.package.disassemble(index):
                native = instruction.get("method", {})
                if (index, instruction["offset"]) in executed and native.get("class_name") == "ed/base/External":
                    native_abi[native["index"]] = {key: native[key] for key in ("index", "class_name", "name", "descriptor")}
        return {"scene": name, "scene_id": scene_id,
                "scene_payload_sha256": hashlib.sha256(self.payload).hexdigest(),
                "script_sha256": self.script_sha256,
                "constructor_method": constructor, "activation_method": method,
                "activation_code_offset": method_data["code_offset"],
                "activation_code_length": method_data["code_length"],
                "activation_fresh_argument": False, "input_fields": restored_fields or {},
                "portal_enabled": dict(self.portal_enabled),
                "disabled_volumes": [i for i, value in self.volume_enabled.items() if not value],
                "state_changes": {key: value for key, value in self.vm.statics.items()
                                  if isinstance(value, int) and before.get(key) != value},
                "state_calls": list(self.activation_calls),
                "native_abi": [native_abi[index] for index in sorted(native_abi)],
                "executed_methods": sorted({index for index, _ in self.vm.trace}), "steps": self.vm.steps}


def run_case(game, scenes=SCENES):
    results, presentation_cases = [], []
    for name in scenes:
        for progression in (0, 1):
            for cord_used in (0, 1):
                case = SceneActivationCase(game)
                results.append(case.activate_arrival(name, {405: progression, 435: cord_used}))
        case = SceneActivationCase(game)
        method = case.vm.method_index("Scene" + name.upper(), "activateScene", "(Z)V")
        restored = {item["field_index"]: 1 for item in case.package.disassemble(method)
                    if item["mnemonic"] == "getstatic" and 400 <= item.get("field_index", 0) < 700}
        # Coverage of already-collected object visibility / one-time movie flags;
        # this combination is not asserted to be a naturally reached save state.
        presentation_cases.append(case.activate_arrival(name, restored))
    abi = {native["index"]: native for result in results + presentation_cases for native in result["native_abi"]}
    return {"cases": results, "presentation_branch_cases": presentation_cases,
            "native_abi_union": [abi[index] for index in sorted(abi)],
            "object_pool": {"initialize_method": 843, "initialize_callers": [[815, 6], [1783, 3]],
                            "release_method": 844, "release_caller": [1782, 65],
                            "slots": 32, "slot_field": 42, "state_field": 697, "usage_field": 698,
                            "aux_field": 699, "initialized_static": 701,
                            "semantics": "Original constructor835 and initialize843 create indexed Obj references. Original reinitialize844 releases usage before the next scene activation; no host object-pool predicate replaces the scripts."},
            "limits": "Full original constructors and arrival activation execute. Audio, lights, objects, movies and GUI are headless engine fixtures. Fresh-start branches and per-frame scene logic are separate."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--scene", choices=SCENES)
    parser.add_argument("--cord-used", type=int, choices=(0, 1), default=0)
    args = parser.parse_args()
    result = (SceneActivationCase(args.game).activate_arrival(args.scene, {435: args.cord_used})
              if args.scene else run_case(args.game))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
