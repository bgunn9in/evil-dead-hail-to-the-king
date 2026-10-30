"""Verify the bounded original PC screen-effect instruction slice read-only."""

import argparse
import hashlib
import json
from pathlib import Path

from audit_scene_coverage import literal_arguments
from ed_scripts import ScriptPackage


ROOT = Path(__file__).resolve().parents[1]


def audit(game):
    import capstone
    import pefile

    game = Path(game)
    data = (game / "EvilDead.exe").read_bytes()
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    instructions = {
        0x42F7CF: ("add", "ax, 8"), 0x42F7D3: ("cmp", "ax, 0xff"),
        0x42F81A: ("add", "ax, 0x10"), 0x42F81E: ("cmp", "ax, 0xff"),
        0x42FC5A: ("sar", "eax, 3"), 0x42FC5D: ("neg", "eax"),
        0x42FC5F: ("cmp", "ax, 0xffff"), 0x42FC6B: ("cmp", "ax, 1"),
        0x423208: ("call", "0x42fc40"),
        0x417AB2: ("call", "0x42f7b0"), 0x417ACB: ("call", "0x42f7b0"),
        0x41D95C: ("call", "0x417b00"), 0x41D961: ("call", "0x42fd00"),
        0x42FD0B: ("call", "0x420690"), 0x4206FA: ("call", "0x40cdb0"),
    }
    for address, expected in instructions.items():
        row = next(decoder.disasm(pe.get_data(address - base, 15), address), None)
        if row is None or (row.mnemonic, row.op_str) != expected:
            raise ValueError(f"Original screen-effect instruction changed at{address:08X}")
    complete = list(decoder.disasm(pe.get_data(0x417AA0 - base, 0x33), 0x417AA0))
    if [r.op_str for r in complete if r.mnemonic == "call"] != ["0x42f7b0", "0x42f7b0"]:
        raise ValueError("scrFxComplete gained an unknown worker")
    result = {
        "format": "ED-SCREEN-EFFECTS1", "original_executable_run": False,
        "executable_sha256": hashlib.sha256(data).hexdigest(),
        "api_references": [
            {"url": "https://raw.githubusercontent.com/wine-mirror/wine/master/include/d3d8types.h",
             "use": "Primary D3D8 header confirms blend5=SRCALPHA,6=INVSRCALPHA; render states7=ZENABLE,19=SRCBLEND,20=DESTBLEND,27=ALPHABLENDENABLE; FVF diffuse/specular/XYZRHW."},
            {"url": "https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dblend",
             "use": "Microsoft description of the shared blend-factor meanings. Original state values are established independently from the D3D8 header and EXE."}],
        "abi": {"scrFxBusy": "417A60 native boolean query; no import in either supplied JPC", "scrFxComplete()V": "417AA0",
                "scrFxLetterbox(Z)V": "417AE0", "scrFxFadeOut()V": "417B00",
                "scrFxFadeIn()V": "417B30", "scrFxShake(I)V": "417B60"},
        "global_state": {"4368CB0": "signed16 letterbox enable", "4368CB2": "out requested",
                         "4368CB3": "out done", "4368CB4": "in requested", "4368CB5": "in done",
                         "4368CB6": "out uint16 progression", "4368CB8": "in uint16 progression",
                         "4368CBA": "signed16 shake amplitude", "4336998": "last float shake offset",
                         "433699C": "float letterbox height"},
        "fade": {"clock": "Overlay worker calls42F7B0; no clock read, dt, VM tick or mode check.",
                 "start": "FadeOut/FadeIn select one request, clear both completion flags and reset only that direction's progression. The other progression word is retained.",
                 "out": "Add8, clamp only when>255, draw black with resulting alpha.32calls from fresh start.",
                 "in": "Add16, clamp only when>255, draw black with alpha255-progression.16calls from fresh start.",
                 "done": "Completed direction no longer submits an overlay. There is no permanent black hold.",
                 "quad": "42FB60 writes four XYZRHW vertices spanning0..640,0..480,z0.5,rhw0.5; diffuse=(alpha<<24), RGB0. State15 enables SRC_ALPHA/INV_SRC_ALPHA and disables Z; two triangles.",
                 "out_alpha_golden": [min(i * 8, 255) for i in range(1, 33)],
                 "in_alpha_golden": [255 - min(i * 16, 255) for i in range(1, 17)]},
        "complete": "417AA0 directly calls only42F7B0 until scrFxBusy-equivalent state clears. No scene/camera worker, event pump, Present, full redraw, or ordinary game tick. Its overlay draw commands occur within the caller's D3D context.",
        "movie_flag4_preroll": "41D952 tests flags&4, calls417B00 then42FD00.42FD00 repeatedly calls420690(1) until outDone. Each render-only pass skips40CDB0 but still runs416320 camera/portal and41C320 projection construction, world/GUI rendering and overlay. No33ms limiter is called by42FD00; duration depends on actual render/Present cadence.",
        "render_only_side_effects": "The whole416320 worker still runs, including its rule/portal predicates and camera countdown gates. Ordinary draw branches still sample actor/object floor BV/cache and can produce render-time VFX children; they do not run VFX simulation, routes, animation, physics, messages or the VM scheduler omitted with40CDB0. Existing4291BC0 blank-frame/portal-state gates can skip world draw while the final overlay worker still runs.",
        "transition_fades": {"common_start": "420E44 starts fadeOut when a scene change is queued.",
                             "same_package": "420E90 requests fadeOut again,420E95 waits42FD00, then4166C0 tears down/recreates the scene.",
                             "restart_AA01_BB01": "420EEE requests fadeOut,420EF3 waits42FD00, then clears the player buffer and sets elapsed integer seconds0.",
                             "normal_cross_package": "420EDA..EEC saves the player and jumps420F0F teardown; it does not wait42FD00. Retain that ordering instead of inventing a32-render pre-load wait.",
                             "arrival": "420FF8 requests fadeIn after final camera/pose setup, for all paths."},
        "shake": {"start": "42FCE0 stores argument low16. Only full argument==0 also resets last offset; nonzero with low16zero retains stale background scalar, matching native.",
                  "sample": "423180 perspective constructor calls42FC40 once. a=-(7*a arithmetic_shift_right3); signed16 result. If -1<=a<=1, set amplitude/offset0. Otherwise offset=f32(a*f32(0.025)); add offset to raw projection matrix[12] and[13].",
                  "projection_not_view": "41C496 calls423180,41C4C5 publishes its matrix as D3DTS_PROJECTION3.",
                  "background_and_masks": "42D1BE and42D3DF read the last scalar, multiply by10 and form tile width128+grow,height160+grow, before original five-column/three-row tile placement. A composed640x480 image scales from top-left by(width/128,height/160); source tileindex0 remains the bottom row. This is not a plain whole-screen translation.",
                  "clock": "Perspective construction calls, including render-only fade passes; scrFxComplete's overlay-only loop does not decay shake.",
                  "amplitude100_golden": [-87, 77, -67, 59, -51, 45, -39, 35, -30, 27]},
        "letterbox": "Optional companion state in42F7B0: signed16 enable; enabling with nonzero full argument resets float height0. Per overlay call grow+2 to32 or shrink-4 to0. Opaque black top/bottom bars in original640x480 coordinates; state14 disables Z after state0 disables alpha blending.",
        "lifetime": "No other direct writer of these globals was found in the executable text. Scene/package changes must not silently reset the effect state; native transition explicitly requests fadeOut/fadeIn.",
        "native_ranges": [], "packages": {}}
    for start, end in ((0x417A60, 0x417B6C), (0x42F7B0, 0x42FD1D),
                       (0x423180, 0x42321C), (0x41D952, 0x41D966),
                       (0x42D1BE, 0x42D1F9), (0x42D3DF, 0x42D402),
                       (0x42E717, 0x42E772), (0x42EFCC, 0x42F129), (0x420690, 0x4207CF),
                       (0x420E38, 0x420F0F), (0x420FDC, 0x421004)):
        raw = pe.get_data(start - base, end - start)
        result["native_ranges"].append({"start_va": f"0x{start:08X}", "end_va_exclusive": f"0x{end:08X}",
                                        "file_offset": pe.get_offset_from_rva(start - base),
                                        "sha256": hashlib.sha256(raw).hexdigest()})
    for tag in "AB":
        data = (game / f"data/script/pc/level{tag}.jpc").read_bytes()
        package = ScriptPackage(data)
        rows = []
        native_names = {key.split("(", 1)[0] for key in result["abi"]}
        for method in package.methods:
            code = package.disassemble(method["index"])
            for index, row in enumerate(code):
                called = row.get("method", {})
                if called.get("class_name") != "ed/base/External" or called.get("name") not in native_names:
                    continue
                argc = int(called["descriptor"] in ("(I)V", "(Z)V"))
                rows.append({"method": method["index"], "owner": method["class_name"], "name": method["name"],
                             "pc": row["offset"], "native": called["name"], "descriptor": called["descriptor"],
                             "literal_arguments": literal_arguments(code, index, argc) if argc else []})
        result["packages"][tag] = {"sha256": hashlib.sha256(data).hexdigest(), "calls": rows}
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--output", type=Path, default=ROOT / "research/source-screen-effects-format.json")
    args = parser.parse_args()
    if args.output.resolve() == args.game.resolve() or args.game.resolve() in args.output.resolve().parents:
        raise ValueError("Evidence must be outside original game directory")
    result = audit(args.game)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("Verified fade/shake instructions and source calls:", {k: len(v["calls"]) for k, v in result["packages"].items()})
