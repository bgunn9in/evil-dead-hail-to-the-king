"""Record source-proven GUI paging/sprite boundaries without running the EXE."""
import hashlib
import json
from pathlib import Path

from ed_gui_assets import read_assets
from ed_scripts import ScriptPackage

ROOT = Path(__file__).resolve().parents[1]


def audit(game):
    import pefile
    game = Path(game)
    raw = (game / "EvilDead.exe").read_bytes()
    pe = pefile.PE(data=raw)
    ranges = ((0x40B760, 0x40B77F), (0x40BBF0, 0x40BC0F), (0x41FDD0, 0x4200AA),
              (0x4096C0, 0x409B96), (0x409BA0, 0x409F9F), (0x40A080, 0x40A180),
              (0x4200B0, 0x4203E7), (0x41E0B0, 0x41E1F9))
    native = []
    for start, end in ranges:
        offset = pe.get_offset_from_rva(start - pe.OPTIONAL_HEADER.ImageBase)
        native.append({"start_va": hex(start), "end_exclusive_va": hex(end), "file_offset": offset,
                       "bytes": end - start, "sha256": hashlib.sha256(raw[offset:offset + end - start]).hexdigest()})
    packages = {}
    for level in "AB":
        script = (game / f"data/script/pc/level{level}.jpc").read_bytes()
        package = ScriptPackage(script)
        methods = []
        for method in package.methods:
            if ((method["class_name"], method["name"]) not in {("GameTextBox", "initialize"), ("GameTextBox", "setTextAssetID"),
                    ("InvBookText", "initialize"), ("InvBookText", "setTextAssetID"), ("GameOver", "initialize"),
                    ("GameOver", "enable"), ("GameOver", "disable"), ("Gui", "itemAdvanceText")}):
                continue
            offset, size = 56 + method["code_offset"], method["code_length"]
            methods.append({"index": method["index"], "owner": method["class_name"], "name": method["name"],
                            "signature": method["descriptor"], "file_offset": offset, "bytes": size,
                            "sha256": hashlib.sha256(script[offset:offset + size]).hexdigest()})
        assets = read_assets(game, level)
        packages[level] = {"script_sha256": hashlib.sha256(script).hexdigest(), "methods": methods, "scoped_texts": len(assets["texts"])}
    return {"format": "SOURCE-GUI-PRESENTATION1", "original_executable_run": False,
            "executable_sha256": hashlib.sha256(raw).hexdigest(), "native": native, "packages": packages,
            "paging": {"request": "40B760 ignoresarg2; item.flags|=0x200.40BBF0 alwaysresets signed16item+2cursor0 whenDataIDset.",
                       "render": "41FDD0 resolves scopedtext, createsnativewindow, font14x22, invokes4096C0. It rendersCURRENTpage thenconsumesrequest: nonnullnextpointer -> signed16byteoffset; EOF ->0; clear0x200. Withoutrequestcursorunchanged.",
                       "layout": "4096C0 wholewordwrap; leadinglinewhitespaceignored, space14/tab112; LF and@ linebreaks; insufficientfirstword returnscurrentpointer withoutforcedsplit. $A +9decimalRGBdigits setsARGBcolor,$Creset. $Bfontchange isstrictunsupported: noneofthe835exportedA/Btextrecordsusesit.",
                       "font": "4095AB createsfont0flags1, sourcefont5.bmp18x24cells/14columns. Drawdestination14x22.409CC0 mapscaret^ toapostrophe27h.40A11F overwritesASCIIlookupascending, soLASTduplicatecharsetentrywins. Missingglyphstilladvances14.",
                       "bounds": "AuthoredMaya boundingXY*20 around320,240; signed16item/systemoffsets withnativehalf-coordinate rounding. Mode0 gamewindow orspecifiedmode3GUIindices get8pxpadding +fullouterblackARGB80000000; othermodespadding0/backgroundoff.",
                       "save": "EDGUI3 explicitnonnegative signed16cursor; readsEDGUI2as0. Catalog.validate resolvesallocated+visibleitem/system cursors, plusanexplicitforcedmodernDocitem; rejectsbadcursorbeforeWorldcommit. Hiddenoldscenetextmaypersistunresolvedasnative41FDD0skipsit. DynamicFFFFuseshostresolver."},
            "game_over": {"mesh": "0B142088", "texture": "0B942034", "part": 0,
                          "transform": "41E0B0: px=320+20*sourceX+signed16itemAndSystemOffsetX; py=240-20*sourceY+offsetY; z0.5; originalUVunchanged; constantwhite46E674. SourceGameOverGuiSys/itemvisibilityonly; no inventedmode4/text.",
                          "quad": "6trianglevertices frompart0; retainall8originalparts inEDOBJ1asset.256x256originalRGBAatlas, no upscaling.",
                          "lifetime": "Ash.think709 loopcounter<120 containscounter60show andunreachablecounter120hide. PreservevisibleoldVMuntilnativeAA/BBswitch; freshGameOver.initialize->disableprovidesactualhide. Do notinventearlyhiddenframe."},
            "tests": ["runtime/source_gui_text_tests.cpp", "runtime/source_gui_presentation_tests.cpp", "tools/test_ed_gui_assets.py"]}


if __name__ == "__main__":
    result = audit(ROOT / "game")
    (ROOT / "research/gui-text-format.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print({key: value["scoped_texts"] for key, value in result["packages"].items()})
