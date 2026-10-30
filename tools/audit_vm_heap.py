"""Read-only reachability census of the modern VM's opaque snapshots.

The graph includes typed VM references, interned literals and suspended frames.
Host-owned references must be supplied separately; this tool never edits a VM.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct


def fnv(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def census(data, external_roots=(), class_names=None):
    if len(data) < 24 or data[:8] not in (b"EDVMST1\0", b"EDVMST2\0", b"EDVMST3\0"):
        raise ValueError("Unsupported bounded VM snapshot")
    if len(data) > 128 * 1024 * 1024 or fnv(data[:-4]) != struct.unpack_from("<I", data, len(data) - 4)[0]:
        raise ValueError("Invalid VM snapshot checksum or size")
    cursor, roots = 8, set()

    def word():
        nonlocal cursor
        if cursor + 4 > len(data) - 4:
            raise ValueError("Truncated VM snapshot")
        value = struct.unpack_from("<I", data, cursor)[0]
        cursor += 4
        return value

    def bounded(limit):
        value = word()
        if value > limit:
            raise ValueError("Excessive VM snapshot count")
        return value

    def reference(value, target):
        if value > count:
            raise ValueError("VM reference outside heap")
        if value:
            target.add(value)

    def value(target, frame=False):
        kind, bits = word(), word()
        if kind > (3 if frame else 1) or (kind > 1 and bits):
            raise ValueError("Invalid typed VM value")
        if kind == 1:
            reference(bits, target)

    def fields(target):
        amount = bounded(1000000)
        for _ in range(amount):
            word()
            value(target)
        return amount

    def values(target):
        amount = bounded(1000000)
        for _ in range(amount):
            value(target, True)
        return amount

    package_hash, count = word(), bounded(100000)
    fields(roots)
    entries, cells = [], 0
    for _ in range(count):
        owner, kind, edges = word(), word(), set()
        amount = fields(edges)
        length = bounded(1000000)
        for _ in range(length):
            value(edges)
        amount += length
        cells += amount
        if cells > 4000000:
            raise ValueError("VM cell budget exceeded")
        entries.append((owner, kind, amount, edges))
    literals = bounded(count)
    for _ in range(literals):
        word()
        reference(word(), roots)
    initialized = bounded(65536)
    for _ in range(initialized):
        word()
    thread_count = 0
    if data[6] >= ord("2"):
        word(); word(); word()  # source frame64 and next thread ID
        thread_count = bounded(1024)
        for _ in range(thread_count):
            for _ in range(9):
                word()  # ID/status/held/wait-kind/actor pair/event/deadline64
            value(roots, True)
            word()  # initial method
            reference(word(), roots)
            values(roots)
            for _ in range(bounded(64)):
                word()
                reference(word(), roots)
                word()
                length = bounded(1024)
                if cursor + length > len(data) - 4:
                    raise ValueError("Truncated VM result descriptor")
                cursor += length
                values(roots); values(roots); values(roots)
            if data[6] >= ord("3"):
                if word() > 1 or word() > 1:
                    raise ValueError("Invalid VM thread flags")
    if cursor != len(data) - 4:
        raise ValueError("Trailing VM snapshot bytes")
    external = set(external_roots)
    for ref in external:
        reference(ref, roots)
    reachable, pending = set(), list(roots)
    while pending:
        ref = pending.pop()
        if ref in reachable:
            continue
        reachable.add(ref)
        pending.extend(entries[ref - 1][3] - reachable)

    def group(indices):
        names = Counter()
        for index in indices:
            owner, kind, _, _ = entries[index - 1]
            name = "array:" + chr(kind) if kind else (class_names or {}).get(owner, "class:" + str(owner))
            names[name] += 1
        return dict(names.most_common())

    dead = set(range(1, count + 1)) - reachable
    return {"snapshot_bytes": len(data), "package_hash": package_hash, "objects": count,
            "cells": cells, "threads": thread_count, "interned_strings": literals,
            "external_root_count": len(external), "reachable_objects": len(reachable),
            "unreachable_objects": len(dead), "unreachable_cells": sum(entries[i - 1][2] for i in dead),
            "reachable_classes": group(reachable), "unreachable_classes": group(dead)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--jpc", type=Path)
    args = parser.parse_args()
    names = None
    if args.jpc:
        from ed_scripts import ScriptPackage
        names = {c["index"]: c["name"] for c in ScriptPackage(args.jpc.read_bytes()).classes}
    result = {}
    for path in sorted(args.directory.glob("*.vm")):
        host = json.loads(path.with_suffix(".roots.json").read_text(encoding="utf-8"))
        result[path.name] = census(path.read_bytes(), host["refs"], names)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
