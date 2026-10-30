"""Audit the original PC scene arena, actor registration and teardown call order."""

import argparse
import hashlib
import json
from pathlib import Path
import struct

from ed_actions import EXE_SHA256, _va_reader
from ed_scripts import ScriptPackage


BLOCKS = {
    "arena_mark": (0x40CFE0, 0x17), "arena_rewind": (0x40D000, 0x38),
    "descriptor_unregister": (0x402DD0, 0x27), "physical_register": (0x403830, 0x89),
    "physical_unregister": (0x402E80, 0x2F), "physical_destroy": (0x403900, 0x57),
    "scene_teardown": (0x4166C0, 0x45D), "background_threads_clear": (0x432560, 0x71),
    "route_scene_reset": (0x414C90, 0x61), "actor_update_slots": (0x4031C0, 0x114),
    "actor_draw_slots": (0x402E40, 0x35), "global_animation_slots": (0x4060E0, 0x54),
}


def audit(executable, package):
    if hashlib.sha256(executable).hexdigest() != EXE_SHA256:
        raise ValueError("Actor teardown is verified only for the recorded original PC EXE")
    read = _va_reader(executable)
    first_record = struct.unpack("<I", read(0x402DD2, 4))[0]
    end_record = struct.unpack("<I", read(0x402DEF, 4))[0]
    if (first_record, end_record) != (0x46311C, 0x463464):
        raise ValueError("Unexpected original descriptor range")
    methods = {index: package.methods[index] for index in (639, 1778, 1782, 1783)}
    if [(m["class_name"], m["name"]) for m in methods.values()] != [
            ("ActorDB", "reinitialize"), ("LevelA", "create"),
            ("LevelA", "endScene"), ("LevelA", "createScene")]:
        raise ValueError("Unexpected original LevelA method layout")
    calls = lambda index: [{"pc": item["offset"], "method": item["method"]["index"],
                            "owner": item["method"]["class_name"], "name": item["method"]["name"]}
                           for item in package.disassemble(index) if "method" in item]
    return {
        "exe_sha256": EXE_SHA256,
        "native_blocks": {name: {"va": hex(va), "size": length,
                           "sha256": hashlib.sha256(read(va, length)).hexdigest()}
                          for name, (va, length) in BLOCKS.items()},
        "unregister_descriptor_types": list(range((first_record - 0x463098 - 12) // 24,
                                                  (end_record - 0x463098 - 12) // 24)),
        "physical_actor_slots": read(0x4038A5, 1)[0],
        "level_create_calls": calls(1778), "end_scene_calls": calls(1782),
        "persistent_message_registration_pcs": [item["pc"] for item in calls(1783) if item["method"] == 1581],
        "semantics": {
            "ownership": "40CBDF captures the arena cursor after actual Level.create;4167F5 compares nonplayer actor-data pointers with the saved cursor. Script ref null is not a destruction predicate.",
            "thread_clear": "432560 drops every active slot1+ (queued, waiting, held); slot0 main is retained.416995..416A0A resets all native think handles to-1.",
            "messages": "40D1C0 removes nonplayer listener registrations; LevelA1783 re-registers source Deadite/SkullBat messages. Ash registrations survive.",
            "registration": "403830 first allocates per-type data, then first-null physical13 slot. If full, data/index still exists but is unregistered.402E80 unregisters only;403900 also zeroes actor-data wrapper.",
            "phase_order": "endSceneJPC -> clear nonplayer FSM -> scene arena/resources unload -> remove post-boundary actors -> load scene/routes -> reset preserved nonplayer FSM -> clear background threads/think handles/interests -> actual Main.createScene/LevelA1783.",
            "animation": "4060E0 scans global50 playbacks, not physical13 actors: active/mode!=4, actorflags1 and!40. Registration is not its gate.",
            "modern_cache": "Immutable original mesh/texture assets stay cached by the port. Dynamic native bodies, actions, threads, registrations and source script references follow the recovered lifetime.",
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=Path(__file__).resolve().parents[1] / "game")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    game = args.game.resolve()
    if args.output and (args.output.resolve() == game or game in args.output.resolve().parents):
        parser.error("Research output must remain outside the original game directory")
    result = audit((game / "EvilDead.exe").read_bytes(),
                   ScriptPackage((game / "data/script/pc/levelA.jpc").read_bytes()))
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
