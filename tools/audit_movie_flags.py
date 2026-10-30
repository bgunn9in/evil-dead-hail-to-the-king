"""Read-only inventory of original JPC movie flags and their PC implementation."""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

from audit_scene_coverage import literal_arguments
from ed_movies import movie_name
from ed_scripts import ScriptPackage


ROOT = Path(__file__).resolve().parents[1]


def movie_calls(package):
    wrappers = [m for m in package.methods
                if m["class_name"] == "Fmv" and m["name"] == "moviePlay"]
    by_descriptor = {m["descriptor"]: m for m in wrappers}
    if set(by_descriptor) != {"(I)V", "(II)V"}:
        raise ValueError("Unsupported Fmv.moviePlay overloads")
    one = package.disassemble(by_descriptor["(I)V"]["index"])
    two = package.disassemble(by_descriptor["(II)V"]["index"])
    if ([r["mnemonic"] for r in one] != ["iload_0", "iconst_0", "invokestatic", "return"]
            or one[2].get("method", {}).get("index") != by_descriptor["(II)V"]["index"]
            or [r["mnemonic"] for r in two[-4:]] != ["iload_0", "iload_1", "invokestatic", "return"]
            or two[-2].get("method", {}).get("class_name") != "ed/base/External"
            or two[-2]["method"]["name"] != "moviePlay"):
        raise ValueError("Original default-zero/forwarding wrapper changed")
    result = []
    for method in package.methods:
        code = package.disassemble(method["index"])
        for index, row in enumerate(code):
            called = row.get("method", {})
            if called.get("name") != "moviePlay" or method["class_name"] == "Fmv":
                continue
            if called["class_name"] != "Fmv" or called["descriptor"] not in by_descriptor:
                raise ValueError("Unrecognized direct movie native call")
            arity = 1 if called["descriptor"] == "(I)V" else 2
            arguments = literal_arguments(code, index, arity)
            if arguments is None:
                raise ValueError("Movie arguments are not adjacent source literals")
            identity = arguments[0] & 0xFFFFFFFF
            result.append({"method": method["index"], "owner": method["class_name"],
                           "name": method["name"], "pc": row["offset"],
                           "movie_id": f"0x{identity:08X}", "movie": movie_name(identity),
                           "flags": arguments[1] if arity == 2 else 0})
    return result


def audit(game):
    import pefile

    game = Path(game)
    result = {"format": "ED-MOVIE-FLAGS1", "original_executable_run": False, "packages": {}}
    for tag in "AB":
        data = (game / f"data/script/pc/level{tag}.jpc").read_bytes()
        calls = movie_calls(ScriptPackage(data))
        result["packages"][tag] = {"sha256": hashlib.sha256(data).hexdigest(), "calls": calls,
                                  "flag_counts": dict(sorted(Counter(c["flags"] for c in calls).items()))}
    data = (game / "EvilDead.exe").read_bytes()
    pe = pefile.PE(data=data)
    result["executable_sha256"] = hashlib.sha256(data).hexdigest()
    result["native"] = {
        "External.moviePlay": "0x409050 (II)I; forwards both arguments to41D930 at4090E9..EF",
        "flags2": "No bit2 read in PC movie worker41D930..41DC79; playback is identical to flags0.",
        "flags4": "41D952 tests low flag byte bit4, then417B00 starts black fade and42FD00 renders/waits until its completion before opening Bink.",
        "flags_are_not_BinkOpen_flags": "BinkOpen receives constant0x102000, fallback0x2000, at41D9F0/41DA10/41DA2A.",
        "limits": "Only authored flags0/2/4 are claimed. A modern pre-roll can preserve the fade behavior without executing original D3D/Bink code."}
    result["native_ranges"] = []
    for start, end in ((0x409050, 0x409105), (0x41D930, 0x41DC7A),
                       (0x417B00, 0x417B23), (0x42FD00, 0x42FD1D)):
        raw = pe.get_data(start - pe.OPTIONAL_HEADER.ImageBase, end - start)
        result["native_ranges"].append({"start_va": f"0x{start:08X}", "end_va_exclusive": f"0x{end:08X}",
                                        "file_offset": pe.get_offset_from_rva(start - pe.OPTIONAL_HEADER.ImageBase),
                                        "sha256": hashlib.sha256(raw).hexdigest()})
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--output", type=Path, default=ROOT / "research/movie-flags-format.json")
    args = parser.parse_args()
    output = args.output.resolve()
    if output == args.game.resolve() or args.game.resolve() in output.parents:
        raise ValueError("Evidence must be outside original game directory")
    result = audit(args.game)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print({tag: record["flag_counts"] for tag, record in result["packages"].items()})
