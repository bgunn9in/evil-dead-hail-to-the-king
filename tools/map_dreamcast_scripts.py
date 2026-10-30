"""Compare original JDC/JPC symbols and resolved bytecode without rewriting either."""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re

from ed_scripts import ScriptPackage


ROOT = Path(__file__).resolve().parents[1]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def symbol(method, owners=None):
    return (owners[method["class_index"]] if owners else method["class_name"]), method["name"], method["descriptor"]


def class_keys(package):
    counts = Counter(row["name"] for row in package.classes)
    seen, result = Counter(), []
    for row in package.classes:
        name = row["name"]
        result.append(name if counts[name] == 1 else name + "#declaration_occurrence" + str(seen[name]))
        seen[name] += 1
    return result


def unique(rows, key):
    result = {}
    for row in rows:
        identity = key(row)
        if identity in result:
            raise ValueError("Ambiguous source symbol: " + str(identity))
        result[identity] = row
    return result


def field_slots(package):
    """Class records preserve field ranges; names/descriptors themselves are stripped."""
    result = {}
    owners = class_keys(package)
    for owner in package.classes:
        first, count = owner["raw"][5], owner["raw"][7]
        if first < 0 or count < 0 or first + count > len(package.fields_raw):
            raise ValueError("Invalid declared class field range")
        for ordinal in range(count):
            index = first + ordinal
            if index in result:
                raise ValueError("Overlapping declared class field ranges")
            result[index] = (owners[owner["index"]], ordinal)
    if len(result) != len(package.fields_raw):
        raise ValueError("Class field ranges do not cover the field table")
    return result


def normalized_code(package, method, slots, field_map=None, ignore_field_ordinals=False):
    """Operand relocation proof, not a general optimizer or execution equivalence oracle.

    Preserve numeric ldc bit patterns, opcodes, local slots and switch cases.
    Replace only typed method/class/field operands and relative branch labels.
    ldc/ldc_w share their resolved operation. Anonymous fields are declaration
    slots, never invented human-readable field names.
    """
    code = package.disassemble(method["index"])
    owners = class_keys(package)
    labels = {row["offset"]: index for index, row in enumerate(code)}
    result = []
    for row in code:
        item = ["ldc" if row["opcode"] in (0x12, 0x13) else row["mnemonic"]]
        if "method" in row:
            item.append(["method", *symbol(row["method"], owners)])
            if row["opcode"] == 0xb9:
                item.extend(row["operands"][1:])
        elif "field_index" in row:
            field = row["field_index"]
            if ignore_field_ordinals is True or (ignore_field_ordinals and slots[field][0] in ignore_field_ordinals):
                item.append(["field_owner", slots[field][0]])
            elif field_map is not None:
                item.append(["declaration_slot", *field_map[field]] if field in field_map else ["unresolved_pc_slot", *slots[field]])
            else:
                item.append(["declaration_slot", *slots[field]])
        elif "class_name" in row:
            item.append(["class", owners[row["constant"]["value_raw"]]])
            if row["opcode"] == 0xc5:
                item.append(row["operands"][1])
        elif "constant" in row:
            constant = row["constant"]
            if constant["index_raw"] < 0:
                literal = package.literal_string(constant["value_raw"])
                item.append(["literal_string_image", bytes(literal["chars"]).hex(), literal["offset"], literal["count"]])
            else:
                item.append(["bits32", constant["value_raw"]])
        elif "target" in row:
            item.append(labels[row["target"]])
        elif "cases" in row:
            item += [labels[row["default"]], [[key, labels[target]] for key, target in row["cases"]]]
        else:
            if "wide_opcode" in row:
                item.append(row["wide_opcode"])
            item.extend(row["operands"])
        result.append(item)
    return result


def code_bytes(package, method):
    if not method["has_code"]:
        return b""
    at = 56 + method["code_offset"]
    return package.data[at:at + method["code_length"]]


def first_differences(left, right, limit=4):
    result = []
    for index in range(max(len(left), len(right))):
        a, b = left[index] if index < len(left) else None, right[index] if index < len(right) else None
        if a != b:
            result.append({"instruction": index, "pc": a, "dc": b})
            if len(result) == limit:
                break
    return result


def field_correspondence(pc, dc, pc_methods, dc_methods, pc_slots, dc_slots, pc_classes, dc_classes):
    """Collect one-to-one accesses in equal method skeletons; retain unresolved slots."""
    pairs, inverse, evidence = defaultdict(set), defaultdict(set), defaultdict(list)
    changed_layouts = {name for name in pc_classes.keys() & dc_classes.keys()
                       if pc_classes[name]["raw"][7] != dc_classes[name]["raw"][7]}
    for key in sorted(pc_methods.keys() & dc_methods.keys()):
        a, b = pc_methods[key], dc_methods[key]
        if (a["access_flags"], a["max_stack"], a["max_locals"], a["has_code"]) != (b["access_flags"], b["max_stack"], b["max_locals"], b["has_code"]):
            continue
        if normalized_code(pc, a, pc_slots, ignore_field_ordinals=changed_layouts) != normalized_code(dc, b, dc_slots, ignore_field_ordinals=changed_layouts):
            continue
        for ordinal, (left, right) in enumerate(zip(pc.disassemble(a["index"]), dc.disassemble(b["index"]))):
            if "field_index" not in left:
                continue
            x, y = left["field_index"], right["field_index"]
            pairs[x].add(y);inverse[y].add(x)
            evidence[x, y].append({"method": key, "instruction": ordinal})
    target_slots = {key: index for index, key in dc_slots.items()}
    result, proof = {}, {}
    for index, key in pc_slots.items():
        candidates = pairs[index]
        if len(candidates) == 1 and len(inverse[next(iter(candidates))]) == 1:
            target = next(iter(candidates))
            result[index] = dc_slots[target]
            proof[index] = {"status": "aligned_access_bijection", "dc": target,
                            "evidence_count": len(evidence[index,target]), "evidence_first": evidence[index,target][:4]}
        elif candidates:
            proof[index] = {"status": "conflicting_access_candidates", "dc": None, "candidates": sorted(candidates)}
        elif key[0] in dc_classes and pc_classes[key[0]]["raw"][7] == dc_classes[key[0]]["raw"][7]:
            target = target_slots[key]
            if not inverse[target]:
                result[index] = key
                proof[index] = {"status": "positional_same_owner_count", "dc": target}
            else:
                proof[index] = {"status": "unresolved_target_already_access_mapped", "dc": None}
        elif key[0] not in dc_classes:
            proof[index] = {"status": "pc_only_owner", "dc": None}
        else:
            proof[index] = {"status": "unresolved_owner_layout_changed", "dc": None}
    return result, proof


def compare(pc, dc):
    pc_owners, dc_owners = class_keys(pc), class_keys(dc)
    pc_classes = unique(pc.classes, lambda row: pc_owners[row["index"]])
    dc_classes = unique(dc.classes, lambda row: dc_owners[row["index"]])
    pc_methods = unique(pc.methods, lambda row: symbol(row, pc_owners))
    dc_methods = unique(dc.methods, lambda row: symbol(row, dc_owners))
    pc_slots, dc_slots = field_slots(pc), field_slots(dc)
    field_map, field_proof = field_correspondence(pc, dc, pc_methods, dc_methods, pc_slots, dc_slots, pc_classes, dc_classes)
    counts = Counter()
    classes, methods, fields = [], [], []
    for name in sorted(pc_classes.keys() | dc_classes.keys()):
        a, b = pc_classes.get(name), dc_classes.get(name)
        parent = lambda owners, row: None if not row or row["super_index"] < 0 else owners[row["super_index"]]
        classes.append({"symbol": name, "pc": a["index"] if a else None, "dc": b["index"] if b else None,
                        "pc_parent": parent(pc_owners, a), "dc_parent": parent(dc_owners, b),
                        "duplicate_name_positional": "#declaration_occurrence" in name,
                        "pc_fields": a["raw"][7] if a else 0, "dc_fields": b["raw"][7] if b else 0})
    for key in sorted(pc_methods.keys() | dc_methods.keys()):
        a, b = pc_methods.get(key), dc_methods.get(key)
        row = {"symbol": key, "pc": a["index"] if a else None, "dc": b["index"] if b else None}
        external = key[0] == "ed/base/External"
        row["engine_external"] = external
        row["pc_native_flag"] = bool(a and a["access_flags"] & 0x100)
        row["dc_native_flag"] = bool(b and b["access_flags"] & 0x100)
        row["duplicate_owner_name_positional"] = "#declaration_occurrence" in key[0]
        if not a or not b:
            row["status"] = "pc_only" if a else "dc_only"
        else:
            pc_parent=pc.classes[a["class_index"]]["super_index"]
            dc_parent=dc.classes[b["class_index"]]["super_index"]
            row["owner_parent_equal"]=(pc_owners[pc_parent] if pc_parent>=0 else None)==(dc_owners[dc_parent] if dc_parent>=0 else None)
            left, right = normalized_code(pc, a, pc_slots, field_map), normalized_code(dc, b, dc_slots)
            row.update(pc_code_sha256=digest(code_bytes(pc, a)), dc_code_sha256=digest(code_bytes(dc, b)),
                       pc_instructions=len(left), dc_instructions=len(right),
                       normalized_pc_sha256=digest(json.dumps(left, separators=(",", ":")).encode()),
                       normalized_dc_sha256=digest(json.dumps(right, separators=(",", ":")).encode()))
            metadata = (a["access_flags"], a["max_stack"], a["max_locals"], a["has_code"])
            other = (b["access_flags"], b["max_stack"], b["max_locals"], b["has_code"])
            row["metadata_equal"] = metadata == other
            row["status"] = "resolved_image_equal" if left == right and metadata == other else "changed"
            if row["status"] == "changed":
                row["first_differences"] = first_differences(left, right)
                row["pc_metadata"], row["dc_metadata"] = metadata, other
        counts[row["status"]] += 1
        if external:
            counts["external_" + row["status"]] += 1
        methods.append(row)
    uses = {"pc": defaultdict(Counter), "dc": defaultdict(Counter)}
    for label, package in (("pc", pc), ("dc", dc)):
        for method in package.methods:
            for instruction in package.disassemble(method["index"]):
                if "field_index" in instruction:
                    uses[label][instruction["field_index"]][instruction["mnemonic"]] += 1
    for index, key in sorted(pc_slots.items()):
        proof = field_proof[index]
        other = proof["dc"]
        fields.append({"declaration_slot": key, "pc": index, "dc": other,
                       **proof,
                       "pc_raw": pc.fields_raw[index], "dc_raw": dc.fields_raw[other] if other is not None else None,
                       "pc_accesses": dict(uses["pc"][index]), "dc_accesses": dict(uses["dc"][other]) if other is not None else {}})
    mapped = {row["dc"] for row in fields if row["dc"] is not None}
    for index, key in sorted(dc_slots.items()):
        if index not in mapped:
            fields.append({"declaration_slot": key, "pc": None, "dc": index, "status": "unpaired_dc_slot", "dc_raw": dc.fields_raw[index]})
    stripped = all(row[:2] == [-12851, -12851] for package in (pc, dc) for row in package.fields_raw)
    opcode_histograms, platform_calls = {}, {}
    for label, package in (("pc", pc), ("dc", dc)):
        histogram, calls = Counter(), []
        for method in package.methods:
            for instruction in package.disassemble(method["index"]):
                histogram[instruction["mnemonic"]] += 1
                target = instruction.get("method")
                if target and target["class_name"] == "ed/base/External" and target["name"] == "platform":
                    calls.append({"method": symbol(method), "instruction_offset": instruction["offset"]})
        opcode_histograms[label] = dict(sorted(histogram.items()));platform_calls[label] = calls
    return {"summary": {"pc_classes": len(pc.classes), "dc_classes": len(dc.classes),
                        "pc_methods": len(pc.methods), "dc_methods": len(dc.methods),
                        "pc_fields": len(pc.fields_raw), "dc_fields": len(dc.fields_raw),
                        "method_status": dict(sorted(counts.items())), "field_status": dict(sorted(Counter(row["status"] for row in fields).items())),
                        "field_names_descriptors_all_cdcd": stripped},
            "limits": ["Resolved-image equality proves only the stated declaration-slot/operand normalization, not general program equivalence.",
                       "Duplicate class names use explicit declaration-occurrence suffixes; these pairs remain positional/ambiguous, not unique symbolic names.",
                       "Field names/types are stripped. Owner+ordinal is explicit positional metadata, not a recovered human field name or permission for blanket runtime redirection.",
                       "Numeric ldc payloads are unchanged bit patterns; embedded callback IDs, if any, remain differences until their typed ABI is proved.",
                       "Native dummy-body equality does not prove native implementation equivalence."],
            "opcode_histograms": opcode_histograms, "platform_native_callers": platform_calls,
            "classes": classes, "methods": methods, "fields": fields}


def masked_cpp(text):
    # Keep positions/newlines, but prevent comments/strings from becoming call sites.
    pattern = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(pattern, lambda match: "".join("\n" if c == "\n" else " " for c in match[0]), text)


def cpp_inventory(runtime):
    rows, files = [], []
    for path in sorted(Path(runtime).rglob("*")):
        if not path.is_file() or path.suffix not in (".h", ".cpp", ".inl"):
            continue
        raw = path.read_bytes()
        source = raw.decode("utf-8")
        files.append({"path": path.relative_to(runtime).as_posix(), "bytes": len(raw), "sha256": digest(raw)})
        masked = masked_cpp(source)
        for match in re.finditer(r'(?:\.|->)\s*(field|setField|staticField|invoke|start|enqueue|startCallback|makeObject|classIndex|methodIndex)\s*\(', masked):
            at, depth, args, begin = match.end(), 1, [], match.end()
            while at < len(masked) and depth:
                char = masked[at]
                if char == "(": depth += 1
                elif char == ")": depth -= 1
                elif char == "," and depth == 1:
                    args.append(source[begin:at].strip());begin = at + 1
                at += 1
            if depth:
                raise ValueError("Unbalanced C++ call in " + str(path))
            args.append(source[begin:at-1].strip())
            api = match[1]
            slot = 1 if api in ("field", "setField") else 0
            argument = args[slot] if slot < len(args) else ""
            literal = re.fullmatch(r'(0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*', argument)
            rows.append({"path": path.relative_to(runtime).as_posix(), "line": source.count("\n", 0, match.start()) + 1,
                         "api": api, "index_literal": int(literal[1],16 if literal[1].lower().startswith("0x") else 10) if literal else None, "argument": argument,
                         "scope": "test" if "_tests" in path.name else "production"})
        for match in re.finditer(r'(?:\.|->)\s*(statics|methods|classes)\s*(\[|\.at\s*\()', masked):
            opening = '[' if match[2] == '[' else '('
            closing = ']' if opening == '[' else ')'
            at, depth, begin = match.end(), 1, match.end()
            while at < len(masked) and depth:
                if masked[at] == opening: depth += 1
                elif masked[at] == closing: depth -= 1
                at += 1
            if depth:
                raise ValueError("Unbalanced direct C++ table access")
            argument = source[begin:at-1].strip()
            literal = re.fullmatch(r'(0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*', argument)
            rows.append({"path": path.relative_to(runtime).as_posix(), "line": source.count("\n", 0, match.start())+1,
                         "api": match[1]+"_direct", "index_literal": int(literal[1],16 if literal[1].lower().startswith("0x") else 10) if literal else None,
                         "argument": argument, "scope": "test" if "_tests" in path.name else "production"})
    return {"limit": "Lexical balanced-call and direct statics/methods/classes inventory. Nonliteral wrappers/initializer tables/default arguments remain explicit review boundaries; this is not a C++ AST/dataflow proof.",
            "summary": {scope: {"calls": sum(r["scope"] == scope for r in rows),
                                 "numeric_calls": sum(r["scope"] == scope and r["index_literal"] is not None for r in rows),
                                 "by_api": dict(sorted(Counter(r["api"] for r in rows if r["scope"] == scope).items()))}
                        for scope in ("production", "test")}, "files": files, "sites": rows}


def resolve_cpp_inventory(cpp, reports):
    for site in cpp["sites"]:
        index = site["index_literal"]
        if index is None:
            continue
        kind = ("fields" if site["api"] in ("field","setField","staticField","statics_direct") else
                "methods" if site["api"] in ("invoke","start","enqueue","startCallback","methods_direct") else
                "classes" if site["api"] in ("makeObject","classes_direct") else None)
        if not kind:
            continue
        resolution = {}
        for level, report in reports.items():
            matches = [row for row in report[kind] if row["pc"] == index]
            if len(matches) != 1:
                resolution[level] = {"status":"outside_source_table"}
                continue
            row = matches[0]
            resolution[level] = {"dc":row["dc"],"symbol":row.get("symbol",row.get("declaration_slot")),
                                 "status":row.get("status","class_name_match"),"index_changes":row["dc"]!=index}
        site["pc_to_dc"] = resolution
    cpp["literal_migration_summary"] = {
        scope: {level: {"mapped_literal_sites":sum(site["scope"]==scope and level in site.get("pc_to_dc",{}) for site in cpp["sites"]),
                        "changed_index_sites":sum(site["scope"]==scope and site.get("pc_to_dc",{}).get(level,{}).get("index_changes",False) for site in cpp["sites"])}
                for level in reports} for scope in ("production","test")}
    return cpp


def generate(dc_root, pc_root, runtime, output):
    output = Path(output).resolve()
    if output.exists():
        raise FileExistsError("Use a fresh report directory")
    protected=[Path(dc_root).resolve(),Path(pc_root).resolve(),Path(runtime).resolve()]
    pc=Path(pc_root).resolve()
    if tuple(part.lower() for part in pc.parts[-3:])==("data","script","pc"):
        protected.append(pc.parents[2])
    if any(output==source or source in output.parents for source in protected):
        raise ValueError("Reports must be outside original packages and runtime sources")
    reports, inputs = {}, []
    for level in "AB":
        paths = {"pc": Path(pc_root) / f"level{level}.jpc", "dc": Path(dc_root) / f"LEVEL{level}.JDC"}
        data = {key: path.read_bytes() for key, path in paths.items()}
        inputs.extend({"level": level, "platform": key, "path": path.as_posix(), "bytes": len(data[key]), "sha256": digest(data[key])}
                      for key, path in paths.items())
        reports[level] = compare(ScriptPackage(data["pc"]), ScriptPackage(data["dc"]))
    output.mkdir(parents=True)
    report = {"format": "ED-JDC-JPC-SYMBOL-MAP1", "inputs": inputs, "packages": reports}
    (output / "script-map.json").write_text(json.dumps(report, indent=2, ensure_ascii=False)+"\n", encoding="utf-8")
    cpp = resolve_cpp_inventory(cpp_inventory(runtime),reports)
    (output / "cpp-callers.json").write_text(json.dumps(cpp, indent=2, ensure_ascii=False)+"\n", encoding="utf-8")
    return {"packages": {level: row["summary"] for level, row in reports.items()}, "cpp": cpp["summary"]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dc", type=Path, required=True)
    parser.add_argument("--pc", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, default=ROOT/"runtime")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(generate(args.dc, args.pc, args.runtime, args.output), indent=2))
