"""Check the original PC scheduler's pause mask and verified thread-launch flags."""

import argparse
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def audit(game):
    import capstone
    import pefile

    raw = (Path(game) / "EvilDead.exe").read_bytes()
    pe = pefile.PE(data=raw)
    base = pe.OPTIONAL_HEADER.ImageBase
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    expected = {
        0x4325E7: ("inc", "ecx"),
        0x4325E9: ("mov", "dword ptr [0x433be68], ecx"),
        0x43261D: ("mov", "eax, dword ptr [esp + 0x14]"),
        0x432621: ("mov", "ecx, dword ptr [esi + 0x2a0]"),
        0x432627: ("test", "eax, ecx"),
        0x432629: ("jne", "0x432756"),
        0x43264D: ("cmp", "ecx, eax"),
        0x401952: ("push", "1"),
        0x40195B: ("call", "0x432c80"),
        0x40D694: ("push", "1"),
        0x40D6A0: ("call", "0x433380"),
        0x40BD6B: ("push", "0"),
        0x40BD77: ("call", "0x4331b0"),
        0x40BD97: ("push", "0"),
        0x40BDA3: ("call", "0x4331b0"),
        0x40B4C1: ("push", "0"),
        0x40B4CD: ("call", "0x4331b0"),
    }
    for address, instruction in expected.items():
        actual = next(decoder.disasm(pe.get_data(address - base, 15), address), None)
        if actual is None or (actual.mnemonic, actual.op_str) != instruction:
            raise ValueError(f"Original scheduler/launch instruction changed at {address:08X}")
    ranges = [(0x4325E0, 0x432675), (0x401943, 0x401960), (0x40D68C, 0x40D6A5),
              (0x40BD6B, 0x40BD7C), (0x40BD97, 0x40BDA8), (0x40B4C1, 0x40B4D2)]
    evidence = [{"start_va": hex(start), "end_va_exclusive": hex(end),
                 "file_offset": pe.get_offset_from_rva(start - base),
                 "sha256": hashlib.sha256(pe.get_data(start - base, end - start)).hexdigest()}
                for start, end in ranges]
    return {
        "format": "ED-THREAD-MASK1", "executable_sha256": hashlib.sha256(raw).hexdigest(),
        "original_executable_run": False, "native_ranges": evidence,
        "mask": "Caller mask AND thread+2A0 skips a thread before its frame/time deadline check. Scheduler still increments global433BE68 every ordinary frame.",
        "launch_flags": {"actor_think": 1, "actor_message_callback": 1,
                         "gui_item_callback": 0, "gui_system_callback": 0, "gui_focus_callback": 0},
        "mode": "Applied464040==0 selects scheduler mask0; other modes select1. Main/Interface execute immediately before the gated simulation phases. Pending464050 is applied at end40A1C0. See menu-context-format.json.",
        "runtime": "Interpreter advanceFrame(mask)/runReady(mask), GameplayActors.update(tick,simulate,mask). Messages dispatch synchronously before the scheduler even while gameplay is masked. SourceGui uses startCallback(flags0), including reentrant native focus callbacks.",
        "persistence": "EDVMST3 appends flags and detached-callback ownership per thread. V1/V2 read with gameplay flag1; actor thinker/message references require flag1 and owned lifetime. Invalid snapshots stage no mutation.",
        "checks": ["jpc_tests::phaseMaskTests: nested callback/yield, UI0 runs under mask1, gameplay1 frame wait stays parked then wakes, V2/V3 save replay and malformed flags/ownership rejection",
                   "gameplay_actors_tests::menuPhaseTests: real ActorDB649->Ash703 damages100->80 once under pause, animation/pending movement stay frozen, floors remain live, exact save/resume",
                   "source_gui_input_tests: actual A/B Pad callbacks and nested source focus changes"],
        "limits": "Verified launch flags0/1 only. Independent synchronous native callback nesting is bounded to16 to protect the host stack; ordinary script depth remains64. Existing bytecode/event/frame-wait subset is unchanged.",
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--output", type=Path, default=ROOT / "research/native-thread-mask-format.json")
    args = parser.parse_args()
    target, game = args.output.resolve(), args.game.resolve()
    if target == game or game in target.parents:
        raise ValueError("Evidence must stay outside the original game directory")
    value = audit(game)
    target.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    print("Original scheduler masks and actor/UI launch flags: verified")
