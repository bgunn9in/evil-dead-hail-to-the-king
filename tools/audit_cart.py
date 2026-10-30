"""Inventory the original PC Cart ABI and a safe modern save integration boundary."""

import argparse
import hashlib
import json
from pathlib import Path

from ed_scripts import ScriptPackage

ROOT = Path(__file__).resolve().parents[1]


def audit(game):
    import pefile
    game = Path(game)
    binary = (game / "EvilDead.exe").read_bytes()
    pe = pefile.PE(data=binary)
    registrations = json.loads((ROOT / "research/native-functions.json").read_text(encoding="utf-8"))
    if registrations["source_sha256"] != hashlib.sha256(binary).hexdigest():
        raise ValueError("Native registration evidence belongs to a different executable")
    candidates = {row["name"]: row for row in registrations["candidates"] if row["name"].startswith("cart")}
    packages = {}
    for level in "AB":
        raw = (game / f"data/script/pc/level{level}.jpc").read_bytes()
        package = ScriptPackage(raw)
        methods = [method for method in package.methods if method["class_name"] == "ed/base/External" and method["name"].startswith("cart")]
        entries = []
        for method in methods:
            callers = [{"method": owner["index"], "owner": owner["class_name"], "name": owner["name"], "pc": row["offset"]}
                       for owner in package.methods for row in package.disassemble(owner["index"])
                       if row.get("method", {}).get("index") == method["index"]]
            registration = candidates.get(method["name"])
            if registration is None and callers:
                raise ValueError("Called Cart native has no verified registration")
            entries.append({"index": method["index"], "name": method["name"], "descriptor": method["descriptor"],
                            "native_va": registration["function_va"] if registration else None, "callers": callers})
        packages[level] = {"script_sha256": hashlib.sha256(raw).hexdigest(), "natives": entries,
                           "save_method": next(method["index"] for method in package.methods if method["class_name"] == "Main" and method["name"] == "save"),
                           "restore_method": next(method["index"] for method in package.methods if method["class_name"] == "Main" and method["name"] == "restore")}
    ranges = []
    for start, end in ((0x407160, 0x40722C), (0x407860, 0x407AB6), (0x408610, 0x408838),
                       (0x41C650, 0x41C748), (0x41C860, 0x41C86F), (0x41C8F0, 0x41C8F6),
                       (0x41C900, 0x41C99C), (0x41C9A0, 0x41CB08)):
        data = pe.get_data(start - pe.OPTIONAL_HEADER.ImageBase, end - start)
        ranges.append({"start_va": hex(start), "end_va_exclusive": hex(end),
                       "file_offset": pe.get_offset_from_rva(start - pe.OPTIONAL_HEADER.ImageBase),
                       "sha256": hashlib.sha256(data).hexdigest()})
    return {"format": "ED-CART-BOUNDARY1", "original_executable_run": False,
            "executable_sha256": hashlib.sha256(binary).hexdigest(), "packages": packages, "native_ranges": ranges,
            "source_facts": {
                "slots": "Native catalog41C9A0 scans8files slot0.eds throughslot7.eds using slot%d.eds@46E5F4; no VM refs in files.",
                "prefix": "41C9ED reads20-byte ASCII prefix, first12bytes equal EVILDEAD-418@46E604; remaining bytes contain source scene/time fields. Legacy payload decoding is not implemented here.",
                "payload": "41C650 reads20-byte prefix thenremainingbytes into4349440;41C860 advertises0x4000capacity.407160 verifies bufferword+4==0x10001, extracts twointegerargs andarraypointerbuffer+24, invokes Main.restore(II[B)I817.",
                "slot_status": "Native41CACF marks validcatalogrecord2; missing/truncated/wrongprefix records0. cartCheckValidSlot41C900 returns rawstatusbyte; JPC Cart.checkValidSlot masks low4bits.",
                "free_blocks": "41C8F0 returns1024;418C60 card-present returns1. These PC constants do not mean a save file exists.",
                "count": "cartCountValidSlots383 declared()I is registered4083B0 wrapper ->418C60=>1; cartCountValidCardSlots384 ->4083D0->41C920 counts records with rawstatus0. Preserve the actual ABI rather than relying on misleading names.",
                "start_vs_done": "cartLoadGame407860 schedulesstate3 and returns1. cartLoadGameDone407AA0 returns1 onlystate1or2, else0; acceptedrequest is not successfulrestore.",
                "error": "408610 returns4 if nativevalidbyte439D384 is0,2 if439D385 is1,3 if439D386 is1; otherwise servicespendingstate and canreturn0.",
                "text_ids": "Cart.textAssetId736(mode,slot)=FFFF0000|(mode<<8)|slot.41C940 mode1 resolves source31990960;mode2 returns slotdescription onlystatus2, emptylabel forstatus0, null otherwise.",
                "unregistered_unused": "External.cartRefresh547()V has no callsites in either original JPC and no entry in the verified native registration scan; do not stub it as success."},
            "modern_adapter_plan": {
                "status": "Integration proposal; intentionally not a claim of original save-file compatibility.",
                "ownership": "Root owns World/save_file.h/world_save.inl and should stage this adapter there. No main/World changes made by this audit.",
                "slot_mapping": "Explicit modern policy: native slot0 aliases existing quicksave.eds; slots1..7 use slot1.eds..slot7.eds in the same verified preference directory. Modern full-VM envelope remains distinct from original EVILDEAD-418 files.",
                "catalog": "Read actual bounded savefile::read and validate envelope/payload before reporting status2. Missing or invalid files remain unselectable/status0. Expose modern scene/time preview only after validation; no invented valid slots or successful file returns.",
                "load": "Native cartLoadGame queues(slot,load), returns accepted1 onlywhenrequestwasaccepted; process queue after the outer VM/GUI callback has returned. Use transactional World.restoreStateBytes including package staging as needed. Do not restore the currently executing VM from inside its native callback.",
                "completion": "On real failure keep currentmenu/VM/GPU/file unchanged, set native-compatible error4 and completedstate for original errorhandler. On successful modern full-VM load the old menu graph is replaced by savedgraph; do not then replay oldMenu.loadGameEnd or Main.restore817 over the already restored VM.",
                "save": "Queue(slot,save); afterVMreturn call saveStateBytes and savefile::atomicWrite. Markdone/error onlyafter real write result. Preserve existingfile onfailure. Source JPC inventory/token decisions run before native request and are not overridden by host.",
                "other_ops": "Begin/refresh-access build a realcatalog; accessDone reports actualcatalog completion. Delete, if implemented, removes only the slot explicitly selected in the source UI; no bulk-format behavior is inferred for modern files. Beep is verified PC no-op presentation; unknown gameplay natives remainstrict.",
                "checkpoint": "Pendingrequest/status/error/catalog belongto World transaction checkpoint. Reject saving a transientpendingCart operation, or version its exactcontinuation. Validate all GUI refs against incoming restoredVM EDBase ancestry before committing.",
                "tests": ["real original MenMain ordinal1 ->MenSaveLoad.setMode2", "emptycatalog has no selectableLoad", "validslot0 aliases actualF5quicksave",
                          "corrupt/envelope-valid-but-invalidVM filesretain currentmenu and reporterror", "incomingGUI sameNumericRef towrongclass rejected beforecommit",
                          "savewritefailure retainspreviousfile", "loadqueued insidecallback runs onlyafterVMreturn", "A/B package-awareloadusesverifiedWorldtransaction"]}}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--output", type=Path, default=ROOT / "research/cart-modern-adapter-format.json")
    args = parser.parse_args()
    target = args.output.resolve()
    if target == args.game.resolve() or args.game.resolve() in target.parents:
        raise ValueError("Evidence must be outside original game directory")
    result = audit(args.game)
    target.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print({level: len(item["natives"]) for level, item in result["packages"].items()})
