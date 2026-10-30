"""Compact source-only B actor method/default map for native registry integration."""

import argparse
import hashlib
import json
from pathlib import Path

from audit_scene_coverage import literal_arguments
from ed_scripts import ScriptPackage, _integer
from ed_vm import descriptor_types
from export_actor import ROOT, LEVEL_B_ACTORS


def analyze(game):
    data = (Path(game) / "data/script/pc/levelB.jpc").read_bytes()
    package = ScriptPackage(data)
    owners = sorted({definition["source_class"] for definition in LEVEL_B_ACTORS.values()})
    result = []
    for owner in owners:
        methods = []
        for method in package.methods:
            if method["class_name"] != owner:
                continue
            code = package.disassemble(method["index"])
            calls, writes = [], []
            for index, row in enumerate(code):
                if "method" in row:
                    target = row["method"]
                    args = descriptor_types(target["descriptor"])[0]
                    calls.append({"pc": row["offset"], "method_index": target["index"],
                                  "symbol": [target["class_name"], target["name"], target["descriptor"]],
                                  "adjacent_literal_args": literal_arguments(code, index, len(args)) if args else []})
                if row["mnemonic"] in ("putfield", "putstatic"):
                    try:
                        value = _integer(code[index - 1])
                    except (ValueError, IndexError):
                        value = None
                    writes.append({"pc": row["offset"], "field": row["field_index"],
                                   "storage": "static" if row["mnemonic"] == "putstatic" else "instance",
                                   "adjacent_literal_value": value})
            start = 56 + method["code_offset"]
            methods.append({"index": method["index"], "name": method["name"], "descriptor": method["descriptor"],
                            "file_offset": start, "bytes": method["code_length"],
                            "sha256": hashlib.sha256(data[start:start + method["code_length"]]).hexdigest(),
                            "return_only": [row["mnemonic"] for row in code] == ["return"],
                            "returns_zero": [row["mnemonic"] for row in code] == ["iconst_0", "ireturn"],
                            "field_writes": writes, "calls": calls})
        result.append({"class": owner, "native_types": sorted({definition["native_type"] for definition in LEVEL_B_ACTORS.values() if definition["source_class"] == owner}),
                       "methods": methods})
    return {"format": "ED-LEVELB-ACTOR-METHODS1", "source_sha256": hashlib.sha256(data).hexdigest(), "actors": result,
            "limits": "Static source map; literal writes are not a final initialized object state. No conditions are executed or replaced. Anonymous field slots retain their original numeric identities."}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    text = json.dumps(analyze(args.game), indent=2) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
