"""Source native action timing and the real JPC accepted-hit suspension boundary."""

import argparse
import json
from pathlib import Path

from ed_actions import EventWaits, NativeActions, parse_ash_actions
from ed_scripts import ScriptPackage
from ed_vm import ScriptError
from jpc_actor_event_case import AshHitCase


def run_case(game):
    states = parse_ash_actions((game / "EvilDead.exe").read_bytes(), (game / "data/az01.pc").read_bytes())
    sequences = []
    for action, target in ((115, 146), (116, 147), (117, 148), (35, 133)):
        actor = NativeActions(states)
        if not actor.command(action):
            raise ValueError("Original idle rejected a verified native action")
        for _ in range(200):
            actor.tick()
            if actor.events:
                break
        if not actor.events:
            raise ValueError("Verified native action did not produce its source completion event")
        sequences.append({"action": action, "state": target, "animation": states[target]["animation_id"],
                          "events": actor.events, "time_unit": "original animation updates"})

    package = ScriptPackage((game / "data/script/pc/levelA.jpc").read_bytes())
    hit = AshHitCase(package, health=70)
    actor = NativeActions(states)
    waits = EventWaits()
    key = ("ed/base/External", "actorDoConsoleAction", "(III)I")
    hit.vm.natives[key] = lambda kind, index, action: int(actor.command(action))
    hit.vm.natives[("ed/base/External", "stateTransitionIsPossible", "(III)I")] = (
        lambda kind, index, action: int(actor.possible(action)))

    def sleep(kind, index, event):
        waits.sleep(1, kind, index, event)
        raise ScriptError("Original native threadSleepEvent requires suspension")

    hit.vm.natives[("ed/base/External", "threadSleepEvent", "(III)V")] = sleep
    try:
        hit.hit(20)
    except ScriptError as error:
        if "requires suspension" not in str(error):
            raise
    else:
        raise ValueError("Accepted native hit action incorrectly ran through the sleep boundary")
    if (hit.ash.fields[13], hit.ash.fields[8], hit.ash.fields[7], hit.ash.fields[114]) != (50, 35, -1, 1):
        raise ValueError("Original JPC did not stop before its post-wait effects")
    if waits.waiting != {1: (1, 0, 26)}:
        raise ValueError("Original hit wrapper requested a different event")
    for _ in range(12):
        actor.tick()
    if [e[1] for e in actor.events] != [26] or waits.deliver(1, 0, 26) != [1]:
        raise ValueError("Original hit animation did not satisfy its exact script wait")
    return {"source_states": len(states), "timings": sequences,
            "accepted_hit": {"health": hit.ash.fields[13], "requested_action": hit.ash.fields[8],
                             "current_action_before_resume": hit.ash.fields[7], "processing_hit": hit.ash.fields[114],
                             "wait": [1, 0, 26], "completion_updates": 12, "completion_event": 26,
                             "source_call_chain": [649, 703, 255, 97, 542, 372, 594],
                             "resume_implemented_here": False},
            "limits": ["The VM must save and resume its original frames after this native wait; this proof stops at that boundary.",
                       "The playback pool models control/timing/events for one isolated actor; model blending, sounds and attack spatial effects are separate native APIs.",
                       "Original PC frame pacing has not been established; animation update counts are not seconds."]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=Path(__file__).resolve().parents[1] / "game")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = json.dumps(run_case(args.game), indent=2) + "\n"
    if args.output:
        args.output.write_text(result, encoding="utf-8")
    else:
        print(result, end="")
