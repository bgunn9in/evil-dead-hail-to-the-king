"""Execute AK09's original first-visit wave setup up to real actor allocation."""

import argparse
import json
import math
from pathlib import Path
import struct

from ed_vm import Array, ScriptError, int32
from jpc_scene_activation_case import ROOT, SceneActivationCase


SELECTED = "BOUNDED_ORIGINAL_ACTOR_ALLOCATION"


class NativeRandom:
    """Original418FC0: seeded64-slot table and preincremented selector."""
    def __init__(self):
        self.seed, self.index, self.ready, self.table = 12345, 0, False, [0] * 64

    def range(self, a, b):
        width = int32(b - a)
        if not width:
            return a
        if not self.ready:
            for index in range(64):
                self.seed = (self.seed * 0x41C64E6D + 0x3039) & 0xFFFFFFFF
                self.table[index] = (((self.seed * 0x88888889) >> 32) >> 3) % 0x07FFFFFF
            self.ready = True
        self.index += 1
        if self.index >= 64:
            self.index, self.ready = 0, False
        return int32(a + self.table[self.index] % abs(width))

    def bounded(self, bound):
        """Original418E70: one LCG step, sharing the table generator's seed."""
        if not bound:
            return 0
        self.seed = (self.seed * 0x41C64E6D + 0x3039) & 0xFFFFFFFF
        value = (((self.seed * 0x88888889) >> 32) >> 3) % 0x07FFFFFF
        return value % abs(bound)


def native_distance_xz(position, marker):
    rounded = lambda value: struct.unpack("<f", struct.pack("<f", value))[0]
    length = math.hypot(rounded(position[0] - marker[0]), rounded(position[2] - marker[2]))
    return int((length if length else 1.0) * 100)  # Native421B60 returns1 for a zero vector.


class SpawnCase(SceneActivationCase):
    def __init__(self, game):
        super().__init__(game)
        self.milliseconds = 0
        self.requested_actor = None
        self.distance_queries = []
        self.random = NativeRandom()
        self.vm.initialize("ed/base/EDBase")
        self.vm.initialize("ed/base/EDSystem")
        self.vm.natives[("ed/base/External", "getMilliseconds", "()I")] = lambda: self.milliseconds
        self.vm.natives[("ed/base/External", "actorDistanceToMarker", "(III)I")] = self.distance_to_marker
        self.vm.natives[("ed/base/External", "sceneIsMarkerVisibleObj", "(I)I")] = self.visibility_unsupported
        self.vm.natives[("ActorDB", "createType", "(I)LGenericNpc;")] = self.allocation_boundary
        self.vm.natives[("ed/base/External", "random", "(I)I")] = self.random.bounded
        self.vm.natives[("ed/base/External", "randomRange", "(II)I")] = self.random.range

    def distance_to_marker(self, actor_type, actor_index, marker):
        self.actor(actor_type, actor_index)
        if not 0 <= marker < len(self.scene_data["markers"]):
            raise ScriptError("Source spawn marker is missing")
        distance = native_distance_xz(self.position, self.scene_data["markers"][marker])
        self.distance_queries.append([marker, distance])
        return distance

    @staticmethod
    def visibility_unsupported(marker):
        raise ScriptError("This bounded spawn fixture does not implement native marker visibility")

    def allocation_boundary(self, actor_type):
        self.requested_actor = actor_type
        raise ScriptError(SELECTED)

    def first_visit(self, position=None):
        self.activate_arrival("ak09")
        self.vm.statics[309] = 0x616B0900
        if position is None:
            position = self.scene_data["markers"][12]
        self.position = list(position)
        # LevelA.create1778 allocates each scene's zero-filled byte[342]. Execute
        # the original bitstream decoder and Spawn.spawnRestore on that data.
        self.vm.invoke(176, args=(Array("B", [0] * 342),))
        self.vm.invoke(962)
        return self.wave(4)

    def wave(self, index):
        array = lambda field: self.vm.statics[field].values
        return {"index": index, "flags": array(793)[index], "interval_ms": array(792)[index],
                "last_spawn_ms": array(791)[index], "max_concurrent": array(783)[index],
                "radius_cm": array(790)[index], "marker_numbers": [marker for slot, marker in enumerate(array(799))
                    if marker != -1 and array(798)[slot] & (1 << index)],
                "object_types": array(796)[index * 4:index * 4 + 4]}

    def frame_until_allocation(self, milliseconds=0):
        self.milliseconds = milliseconds
        self.distance_queries.clear()
        try:
            self.vm.invoke(932)
        except ScriptError as error:
            if SELECTED not in str(error):
                raise
            methods = sorted({method for method, _ in self.vm.trace})
            trace = list(self.vm.trace)
            # After the bounded stop, querying the unchanged positive marker
            # selector again observes its real result without fabricating an NPC.
            slot = self.vm.invoke(1001, args=(4,))
            marker = self.vm.statics[799].values[slot] if slot >= 0 else None
            return {"requested_actor_type": self.requested_actor, "selected_marker": marker,
                    "marker_xyz": self.scene_data["markers"][marker] if marker is not None else None,
                    "distance_queries": self.distance_queries, "executed_methods": methods,
                    "allocation_caller": next(([method, pc] for method, pc in reversed(trace) if method == 970), None),
                    "limits": "Original spawn predicates and marker selection execute; execution deliberately stops before ActorDB.createType657. No NPC, fake object or mission flag is created."}
        return None


def run_case(game):
    case = SpawnCase(game)
    wave = case.first_visit()
    result = case.frame_until_allocation()
    if result is None or result["requested_actor_type"] != 4 or result["selected_marker"] != 12:
        raise ValueError("Original AK09 first Deadite selection changed")
    return {"script_sha256": case.script_sha256, "wave": wave, "selection": result,
            "first_visit_restore_method": 962, "source_zero_bitstream_bytes": 342,
            "frame_method": 932, "clock_ms": 0}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    args = parser.parse_args()
    print(json.dumps(run_case(args.game), indent=2))


if __name__ == "__main__":
    main()
