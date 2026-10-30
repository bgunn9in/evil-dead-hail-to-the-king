"""Read-only PC keyboard-config ABI for a modern controls presentation bridge."""

import argparse
import hashlib
import json
from pathlib import Path
import struct

from ed_scripts import ScriptPackage
from audit_scene_coverage import literal_arguments

ROOT = Path(__file__).resolve().parents[1]


def audit(game):
    import pefile
    game = Path(game)
    raw = (game / "EvilDead.exe").read_bytes()
    pe = pefile.PE(data=raw)
    base = pe.OPTIONAL_HEADER.ImageBase
    table = list(struct.unpack("<5I", pe.get_data(0x40C644 - base, 20)))
    if table != [0x40C597, 0x40C5F9, 0x40C61A, 0x40C627, 0x40C634]:
        raise ValueError("Original keyboard configuration action table changed")
    packages = {}
    for tag in "AB":
        source = (game / f"data/script/pc/level{tag}.jpc").read_bytes()
        package = ScriptPackage(source)
        calls, done = [], []
        for method in package.methods:
            if method["class_name"] in ("MenKeyboardLeft", "MenKeyboardRight") and method["name"] == "inputDone":
                done.append({"owner": method["class_name"], "method": method["index"], "descriptor": method["descriptor"]})
            instructions = package.disassemble(method["index"])
            for index, row in enumerate(instructions):
                target = row.get("method", {})
                if target.get("class_name") == "ed/base/External" and target.get("name") == "guiKeyboardConfigAction":
                    calls.append({"owner": method["class_name"], "method": method["index"], "name": method["name"],
                                  "pc": row["offset"], "literal_arguments": literal_arguments(instructions, index, 2),
                                  "dynamic_row": "ordinal0..6" if method["class_name"] == "MenKeyboardLeft" else
                                                 "ordinal0..6 plus7" if method["class_name"] == "MenKeyboardRight" else None})
        if len(done) != 2:
            raise ValueError("Original keyboard completion callbacks changed")
        packages[tag] = {"sha256": hashlib.sha256(source).hexdigest(), "calls": calls, "completion_callbacks": done}
    ranges = []
    for start, end in ((0x40C580, 0x40C658), (0x4246F8, 0x42475A), (0x4248F6, 0x4249B3),
                       (0x419170, 0x4192D7), (0x40F650, 0x40F690),
                       (0x424EF0, 0x425130), (0x425190, 0x425318)):
        chunk = pe.get_data(start - base, end - start)
        ranges.append({"start_va": hex(start), "end_va_exclusive": hex(end),
                       "file_offset": pe.get_offset_from_rva(start - base), "sha256": hashlib.sha256(chunk).hexdigest()})
    return {"format": "ED-GUI-KEYBOARD1", "original_executable_run": False,
            "executable_sha256": hashlib.sha256(raw).hexdigest(), "packages": packages, "native_ranges": ranges,
            "native": {"abi": "External.guiKeyboardConfigAction(II)I at40C580; arg1action,arg2row foraction3.",
                       "actions1_2": "Subtract3/unsignedswitch means both return0 without nativeeffects. JPC MenKeyboard.enable/disable call1/2, but these are not nativeopen/close.",
                       "action3": "If inhibit42D3844 or capture42D3860 nonzero,return1. Else save oldkeyboardcallback hooks, install40C240/40C150, setcapture42D3860=1,targetrow42D3840=arg2,return0.",
                       "action4": "Restore savedkeyboardcallbackhooks. Does not itself clear42D3860; completion424955 clears it beforecallingaction4.",
                       "action5_6": "5 calls40F670(0)->425190 to rebuild16inputrecords from alternate/menu keytable46EEB4..46EEDC.6 calls40F650(0)->424EF0, reads56B edkeys.cfg into14gameplayDIKs46EE74, thenrebuilds16inputrecords fromgameplaytable. Bothreturn0. JPC MenSaveLoad.enable/disable use5/6; these aremapping modes, not a simulation-pause switch.",
                       "action7": "Return inhibit42D3844!=0; this is not identical to capture42D3860. Other actionvalues return0.",
                       "rows": "Left sourceordinal0..6, Right ordinal0..6+7;14DWORD bindings at46EE74. Modernhost must bounds-checkrow0..13, because originalnative has no such guard.",
                       "default_DI_keys": list(struct.unpack("<14I", pe.get_data(0x46EE74 - base, 56))),
                       "capture_poll": "Whilecapturemode1, nativechecksnew/changed nonzerokeys in ascendingDIKorder0..255. Enter28 andP25 ignored. Escape1 cancels tocompletionwithoutbindingwrite.",
                       "duplicates": "4248FD compares tableINDEX against candidateDIK, skippingthatindex, ratherthanagainsttargetrow. Other matchingbindingvaluesrejectcandidate. Preserve this quirk in sourceclaims; modernControls may have its own declared validationpolicy.",
                       "accepted": "WritecandidateDIK into selectedrow, attemptwrite56bytes toedkeys.cfg (wb), thencompletion.",
                       "completion": "424955 clearcapture;40C580(4,0)restorehooks;424964..4249A5 invoke BOTH staticMenKeyboardLeft.inputDone()V andRight.inputDone()V using synchronous432970 argumentflags0.",
                       "FACE_names": "getText419170 FACE0000|row resolves currentDIK via424080, names via423FB0, suffix/modifier via424090. No inventedFACE labels are exported."},
            "modern_bridge": {"status": "Presentation integration proposal; World/main remain root-owned.",
                              "open": "Let actualJPC enterMenKeyboard; detect activeGUIpad MenKeyboardLeft/Right before renderingSourceMenuText and present existingmodernControlMenu instead of interpretingFACE IDs as staticstrings.",
                              "close": "Invoke actualactiveMenKeyboardLeft.disable()V orRight.disable()V. MenKeyboard.disable thenenablesMenMain in appliedmode1, orPause inmode3; it does notreturntoOptions. Do not manuallyalter menu fields or missionstate.",
                              "row_capture": "If exposing individual originalrowcapture3, stage moderninput capture and reportbusyaccurately. Onaccepted/cancel invoke both originalinputDone callbacks and clearcapture; keeppending GUI/inputstate intransaction checkpoint.",
                              "do_not": "Do not return fakekeynames or treat native1/2 as effects they do not perform. A fullmoderncontrols overlay may use its existing action names/bindings, clearly separate from originalFACE resolver."}}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--output", type=Path, default=ROOT / "research/gui-keyboard-config-format.json")
    args = parser.parse_args()
    if args.output.resolve() == args.game.resolve() or args.game.resolve() in args.output.resolve().parents:
        raise ValueError("Evidence must remain outside original game directory")
    result = audit(args.game)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print({tag: len(value["calls"]) for tag, value in result["packages"].items()})
