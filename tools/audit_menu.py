"""Read-only source audit of PC menus, their input boundary and frame phases."""

import argparse
import hashlib
import json
from pathlib import Path
import struct

from export_menu import ROOT, menu_context, menu_script_audit, resource_pool
from ed_scripts import ScriptPackage


def audit(game):
    import capstone
    import pefile

    game = Path(game)
    raw = (game / "EvilDead.exe").read_bytes()
    pe = pefile.PE(data=raw)
    image_base = pe.OPTIONAL_HEADER.ImageBase
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    ranges = ((0x40CDB0, 0x40CF0C), (0x40A1C0, 0x40A289),
              (0x40B430, 0x40B5A6), (0x40BCD0, 0x40BF5B),
              (0x40B6F0, 0x40B723), (0x40BBB0, 0x40BBE3),
              (0x407160, 0x40722C), (0x407860, 0x407A63),
              (0x407AA0, 0x407AB6), (0x41C650, 0x41C748),
              (0x41C860, 0x41C86F), (0x41C8F0, 0x41C8F6), (0x418C60, 0x418C66),
              (0x402E40, 0x402FC0), (0x41A380, 0x41A42A),
              (0x40E280, 0x40E2F0), (0x423CA0, 0x423D50))
    evidence = []
    for start, end in ranges:
        data = pe.get_data(start - image_base, end - start)
        evidence.append({"start_va": hex(start), "end_va_exclusive": hex(end),
                         "file_offset": pe.get_offset_from_rva(start - image_base),
                         "sha256": hashlib.sha256(data).hexdigest()})
    calls = [(i.address, int(i.op_str, 16)) for i in decoder.disasm(pe.get_data(0x40CDB0 - image_base, 0x12C), 0x40CDB0)
             if i.mnemonic == "call" and i.op_str.startswith("0x")]
    expected = [(0x40CE3E, 0x4060E0), (0x40CE4C, 0x4031C0), (0x40CE5A, 0x40E3A0),
                (0x40CE68, 0x406560), (0x40CE71, 0x418040), (0x40CE79, 0x40D3D0),
                (0x40CE8D, 0x4325E0), (0x40CE9E, 0x411C00), (0x40CEB1, 0x414D00)]
    if any(pair not in calls for pair in expected):
        raise ValueError("Original ordinary frame phase callsites changed")
    packages, death = {}, {}
    for level in "AB":
        context, scripts = menu_context(game, level), menu_script_audit(game, level)
        floor = context["collision"]["floor"]
        count, offset = struct.unpack_from("<II", context["payload"], 8)
        volumes = [{"index": i, "kind": context["payload"][offset + i * 72],
                    "shape": context["payload"][offset + i * 72 + 1],
                    "flags": struct.unpack_from("<I", context["payload"], offset + i * 72 + 64)[0]}
                   for i in range(count)]
        pool = resource_pool(game, level)
        packages[level] = {"scene": context["name"], "scene_id": context["id"],
                           "scene_bytes": len(context["payload"]),
                           "scene_sha256": hashlib.sha256(context["payload"]).hexdigest(),
                           "floor_vertices_xyz": floor["vertices"], "floor_triangles": floor["triangles"],
                           "markers": context["scene"]["markers"], "navpoints": context["scene"]["navpoints"],
                           "camera_frame_counts": [len(track["frames"]) for track in context["scene"]["tracks"]],
                           "volumes": volumes, "native_pool_records": len(pool),
                           "native_pool_bytes": sum(len(row["payload"]) for row in pool), "script": scripts}
        script_bytes = (game / f"data/script/pc/level{level}.jpc").read_bytes()
        package = ScriptPackage(script_bytes)
        thinker = next(method for method in package.methods if method["class_name"] == "Ash" and method["name"] == "think")
        rows = package.disassemble(thinker["index"])
        calls_by_pc = {row["offset"]: row["method"]["name"] for row in rows if row.get("method")}
        if any(calls_by_pc.get(pc) != name for pc, name in ((57, "doConsoleAction"), (87, "setVisible"),
                                                          (100, "setVisible"), (103, "tick"), (188, "sceneSwitch"), (202, "sceneSwitch"))):
            raise ValueError("Original Ash death/title control flow changed")
        modes = set()
        for method in package.methods:
            body = package.disassemble(method["index"])
            for index, row in enumerate(body):
                if row.get("method", {}).get("class_name") == "ed/base/External" and row["method"]["name"] == "gameStateChange":
                    previous = body[index - 1]["mnemonic"]
                    if previous not in {"iconst_0", "iconst_1", "iconst_2", "iconst_3"}:
                        raise ValueError("Original gameStateChange needs a new source-mode audit")
                    modes.add(int(previous[-1]))
        offset, size = thinker["code_offset"], thinker["code_length"]
        death[level] = {"script_sha256": hashlib.sha256(script_bytes).hexdigest(), "think_method": thinker["index"],
                        "code_offset": offset, "file_offset": 56 + offset, "code_bytes": size,
                        "code_sha256": hashlib.sha256(script_bytes[56 + offset:56 + offset + size]).hexdigest(),
                        "direct_requested_modes": sorted(modes)}
    return {"format": "ED-MENU-SOURCE1", "executable_sha256": hashlib.sha256(raw).hexdigest(),
            "original_executable_run": False, "native_ranges": evidence, "packages": packages,
            "gui_input": {"dispatcher": "40BCD0", "system_focus": "40B430",
                          "events": {"97": "focus", "98": "blur", "103": "left", "104": "right",
                                     "105": "up", "106": "down", "111": "PC confirm", "102": "PC back (including OptionsLeft)", "112": "generic cancel (OptionsLeft PC ignores it)"},
                          "item_link_directions": {"0": "left", "1": "right", "2": "up", "3": "down"},
                          "order": "Input goes to selected item Pad.actionItem, then current system Pad.actionSys; native navigation follows callbacks. New selection emits old98/new97. guiSysSet emits oldsystem98/newsystem97 when active system changes.",
                          "thread_flags": "Pad callbacks use launch argument3=0 at40BD6B/40BD97/40B4C1; actor/message callbacks use1. Keep this field separate from execution flags0x95."},
            "death_return": {"source": death, "game_over": "Ash.think health<=0 requests nativeaction64once; loop120 EDNpc.tick calls, showsGameOver atcounter60. The counter120hidebranch isunreachable because loopconditioncounter<120; originalGameOverremainsvisibleuntilfresh titleVM initializationdisablesit. Thenresets100HP/transientfields andqueues AA01/BB01(camera0,marker0,MODEL160degrees). No directsourcegameStateChange4 call; GameOverdoesnotneed a confirmation torestart.",
                             "menu_marker": "Native4035EA unsigned marker>=scene.count returns at403623 without positionwrite. Arrival420F93 calls thatsetter, then420FBF independently appliesEuler401CA0. A menumarker0 withcount0is a positionno-op, not a fabricatedspawn.",
                             "proof": "runtime/death_world_tests.inl: naturalAU04Annie10hits andBC02Cthulhu4hits killAsh withoutHP/message/AIwrites; timedGameOver, freshAA/BB andactualCartLoad recoverprecombatfile. Observed120EDNpc.tickcalls consume241Worldsourceiterations inthis verified VM scheduling path."},
            "pc_cart": {"file_template_va": "46E5F4", "files": "slot%d.eds", "read_mode_va": "46E5F0", "mode": "rb",
                        "read": "41C650 reads20-byte slot-info prefix then remainingfilebytes into4349440;41C860 exposes0x4000capacity. A modern reader must enforce this bound before reading.",
                        "restore": "407160 verifies nativebuffer word+4==0x10001, extracts two integerarguments and array atbuffer+24, calls actual Main.restore(II[B)I817 via4337C0. No VM references cross the file boundary.",
                        "load_start": "407860 selects slot via41C910 and enters nativeCartstate3; it does not indicate payload validation succeeded.",
                        "load_done": "407AA0 returns1onlynativeCartstate1or2, else0.",
                        "free_blocks": "External.cartGetCardFreeBlocks41C8F0 returns constant1024 onPC.",
                        "present": "External.cartIsCardPresent418C60 returns constant1 onPC.",
                        "limits": "Modern Cart uses8bounded EDSAVE1 envelope slots (slot0=quicksave.eds), fullWorld/VM validation and atomic file writes. Native requests onlyqueue; done/error follow actual I/O. Raw original20-byte/16KB saves remain unsupported for import.",
                        "modern_completion": "World format14 stores a prospective completedSave slot onlyin the file being atomicallywritten; liveCart.done remains0untilwrite succeeds. Fullyvalidated load restores that completion with an untrusted/all0catalog, then original MenSaveLoad.processFrame finishes its pendingSave UI without scriptfield writes.",
                        "integration_proof": "runtime/cart_world_tests.inl: actualAA/BBMenMain ordinal1->Access->Load; realF5file, invalidenvelope/VM rollback, sourceSaveUI/atomicwrite/v14completion, optional lateGPUfailure retains oldprofile."},
            "ordinary_frame": {"entry": "40CDB0", "applied_mode_global": "464040", "pending_mode_global": "464050",
                               "applied_interface_static": "Interface field844 (common prefix A/B)",
                               "phases": ["poll input", "Main.processFrame", "Interface.processFrame",
                                          "if appliedMode0: animation", "if appliedMode0: actor physics",
                                          "if appliedMode0: object update", "if appliedMode0: actor callbacks",
                                          "sound", "raw messages", "scheduler (pause mask0 if mode0, else1)",
                                          "if appliedMode0: VFX simulation", "if appliedMode0: routes/helper",
                                          "40A1C0 apply pending mode via Game/Interface callbacks",
                                          "camera/rules/portal worker", "projection", "world draw/floor sampling", "GUI/effects/Present"],
                               "mode_rechecks": "Native reloads applied464040 between simulation groups; a request made inside Main does not become applied until end40A1C0."},
            "render_only": {"entry": "420690(1)",
                            "skips": "Entire40CDB0: Main/Interface/simulation/messages/scheduler stay frozen.",
                            "retains": "Camera/rules/portal/projection/worlddraw/GUI/effects/Present including draw-time ActorBV/objectBV floor caches."},
            "floor_draw_phase": {"actors": "402E40 flags1set/8clear ->401A80->41A380;41A3C3 calls41D5A0;41A3C8 writesaux+100 floorY before visibility41A40A.",
                                 "objects": "40E280 flags1 ->423CB4 flag2 ->423CD8 ActorBV ->423CDD floorY before visibility. No applied-mode0 simulation gate here.",
                                 "menu_visibility": "AA01/BB01 have only kind1volume, no kind3 include volumes. Missing include lists hide native 3D categories without requiring an invented actor pose or world-frustum heuristic."},
            "loader_policy": {"context": "Nonwalking EDMENU1; source nativeScene and real authored floor retained; markers/spawn absent.",
                              "gameplay": "Original Main/Interface select menu mode1. Do not force mode6 (pause), nor run animation/physics/VFX/routes under a pending/applied mismatch.",
                              "load_boundary": "Cart queues outsideVM fullWorld load intoa fresh matchingA/Bcontext; outer owner swaps onlyafter process returns. Catalog2requiresactual fullvalidation. MenMain availability defaults true onPC; it is not a save-count result.",
                              "resources": "EDMRES1 keeps exact sharedMaya/text IDs/flags/bytes plus5originalEXE BEAD0001..5 text aliases verified419170. The scene-local mesh0B4805A8 has no established GUI role; Main uses0B142038/0B942034."},
            "bounded_cpu_proof": {"file": "runtime/source_gui_input_tests.cpp",
                                  "profiles": ["levelA.jpc", "levelB.jpc"],
                                  "checks": ["systemfocus old98/new97 then selecteditem97", "all4bilateral links survive snapshots",
                                             "disableditem skipping and selected0x40 system-link gate", "inputitem->system->navigation order",
                                             "capturedsystemperinputbatch under reentrantcallbacks", "actualPad/MenMain/GuiSys PC111 NewGame->AK01/cam2/marker4/160deg/mode0",
                                             "actualMenMainbusyguard blocksNewGame", "actualMenMainLoadavailability drives setMode2 handoff, notloadsuccess"],
                                  "gui_state": "EDGUI2, linkindices bounded and transactional restore; callbacks execute via VM.startCallback flags0."}}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--output", type=Path, default=ROOT / "research/menu-context-format.json")
    args = parser.parse_args()
    target = args.output.resolve()
    if target == args.game.resolve() or args.game.resolve() in target.parents:
        raise ValueError("Evidence must be outside original game directory")
    value = audit(args.game)
    target.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    print({key: {"scene": item["scene"], "resources": item["native_pool_records"]} for key, item in value["packages"].items()})
