"""Original TroopRange visual7 and ground trail22; NPC damage stays in JPC1531."""
import struct
from pathlib import Path
from ed_actions import _va_reader
from ed_vfx_hits import parse_hit_effects, encode_hit_effects, ReferenceHits


def parse_ranged_effects(exe):
    blank = parse_hit_effects(exe)[1]  # Verifies the original executable hash.
    read = _va_reader(exe)
    f = lambda va: struct.unpack("<f", read(va, 4))[0]
    cap = lambda kind: struct.unpack("<i", read(0x46A724 + kind * 4, 4))[0]
    common = {**blank, "cap": -1, "size_min": 0, "size_max": 0, "growth": 0,
              "frame_mode": 0, "frame_delay": 0, "frame_initial": 0, "required_mode": 0,
              "periodic_child": 0, "child_period": 0, "child_max_age": 0, "child_initial": 0}
    shot = {**common, "kind": 7, "constructor": 0x428120, "cap": cap(7), "life": 30,
            "color": [255] * 4, "delta": [0, 0, 0, -10], "size": f(0x428192 + 3),
            "uv": [f(0x4281E9 + 2), f(0x4281F6 + 3), f(0x4281EF + 3), f(0x4281FD + 3)],
            "columns": 4, "rows": 8, "random_frame": 1, "frame_mode": 2,
            "direction_scale": 1, "periodic_child": 22, "child_period": 2, "child_max_age": 5}
    trail = {**common, "kind": 22, "constructor": 0x42A040, "cap": cap(22), "life": 4,
             "color": [255, 255, 164, 10], "delta": [0, 0, 0, -2], "size": f(0x42A0B4 + 3),
             "growth": f(0x42A0BB + 3), "uv": [f(0x42A10C + 2), f(0x42A119 + 3), f(0x42A112 + 3), f(0x42A120 + 3)],
             "columns": 1, "rows": 1, "random_frame": 0, "direction_scale": 0}
    return [shot, trail]


def encode_ranged_effects(definitions):
    data = bytearray(struct.pack("<8sI", b"EDVFXR1\0", len(definitions)))
    for d in definitions:
        data.extend(encode_hit_effects([d])[12:])
        data.extend(struct.pack("<4I", d["periodic_child"], d["child_period"], d["child_max_age"], d["child_initial"]))
    return bytes(data)


class ReferenceRanged(ReferenceHits):
    """Independent modifier31 shared counter; child22 updates later in the same pass."""
    def __init__(self, definitions, random):
        super().__init__(definitions, random)
        self.child_timers = {d["kind"]: d["child_initial"] for d in definitions if d["periodic_child"]}

    def update(self):
        from ed_vfx import _f32 as f
        for kind in sorted(self.definitions):
            d, particles = self.definitions[kind], self.active.get(kind, [])
            if d["frame_mode"]:
                if self.timers[kind]:
                    self.timers[kind] -= 1
                else:
                    self.timers[kind] = d["frame_delay"]
                    for p in particles:
                        p["tile"] = (p["tile"] + 1) % (d["columns"] * d["rows"])
            for p in particles:
                p["position"] = [f(a + b) for a, b in zip(p["position"], p["velocity"])]
                if p["age"] > d["life"]:
                    p["age"] = -1
            if d["periodic_child"]:
                for p in particles:
                    if self.child_timers[kind]:
                        self.child_timers[kind] -= 1
                        continue
                    self.child_timers[kind] = d["child_period"]
                    if p["age"] <= d["child_max_age"]:
                        self.spawn(d["periodic_child"], p["position"])
        for kind in sorted(self.definitions):
            d = self.definitions[kind]
            self.active[kind] = [p for p in self.active.get(kind, []) if p["age"] >= 0]
            for p in self.active[kind]:
                p["age"] += 1
                p["size"] = f(p["size"] + d["growth"])
                p["color"] = [max(0, min(255, c + delta)) for c, delta in zip(p["color"], d["delta"])]


def source_ranged_event(game):
    from ed_assets import parse_archive
    from ed_model import decode_animations
    archive = (Path(game) / "data/ac01.pc").read_bytes()
    entry = next(e for e in parse_archive(archive) if e.resource_id == 0x07B00504)
    clip = next(c for c in decode_animations(archive[entry.offset:entry.offset + entry.size]) if c["resource_id"] == 0x07B011C4)
    record = bytes.fromhex(clip["events36_raw"][0])
    if struct.unpack_from("<HBBi", record) != (12, 7, 1, -1):
        raise ValueError("Original TroopRange shot animation event changed")
    return record


def ranged_goldens(definitions, event):
    from ed_vfx import SourceRandom
    random = SourceRandom()
    reference = ReferenceRanged(definitions, random)
    reference.spawn(7, [1., 2., 3.], [1.25, 2., 3.5])
    data = bytearray(struct.pack("<8sI", b"EDVFXRG1", 42) + event)
    for tick in range(42):
        if tick == 3:
            reference.spawn(7, [-1., 1., 2.], [-.5, 1.25, 2.])
        if tick:
            reference.update()
        particles = reference.flat()
        data.extend(struct.pack("<6I64I", tick, len(particles), random.seed, random.index, int(random.ready), reference.child_timers[7], *random.table))
        for p in particles:
            data.extend(struct.pack("<IQ7fiI4B", p["kind"], p["serial"], *p["position"], *p["velocity"], p["size"], p["age"], p["tile"], *p["color"]))
    return bytes(data)


def export_ranged_effects(game, output):
    game, output = Path(game).resolve(), Path(output).resolve()
    if game == output or game in output.parents:
        raise ValueError("Ranged VFX exports must stay outside original game directory")
    target = output / "vfx"
    if output not in target.resolve().parents:
        raise ValueError("Ranged VFX output escapes generated assets")
    definitions = parse_ranged_effects((game / "EvilDead.exe").read_bytes())
    target.mkdir(parents=True, exist_ok=True)
    for name, data in (("ranged-effects.bin", encode_ranged_effects(definitions)),
                       ("ranged-goldens.bin", ranged_goldens(definitions, source_ranged_event(game)))):
        path = target / name
        if output not in path.resolve().parents:
            raise ValueError("Ranged VFX output escapes generated assets")
        path.write_bytes(data)
    return definitions
