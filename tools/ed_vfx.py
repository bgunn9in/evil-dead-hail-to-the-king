"""Export original native episode particle definitions, atlas and CPU goldens."""

import argparse
import hashlib
import json
from pathlib import Path
import struct

from ed_actions import EXE_SHA256, _va_reader
from ed_assets import parse_archive
from ed_model import decode_texture
from ed_objects import encode_texture


ATLAS = 0x0816E044
POOL = (0x43916D0 - 0x436FBC0) // 0x5C


def parse_effects(exe):
    if hashlib.sha256(exe).hexdigest() != EXE_SHA256:
        raise ValueError("VFX only verified for the recorded original PC executable")
    read = _va_reader(exe)
    f = lambda address: struct.unpack("<f", read(address, 4))[0]
    u = lambda address: struct.unpack("<I", read(address, 4))[0]
    b = lambda address: read(address, 1)[0]
    signed = lambda value: value - 256 if value > 127 else value
    return [
        {"kind": 9, "constructor": 0x427FA0, "cap": struct.unpack("<i", read(0x46A724 + 9 * 4, 4))[0],
         "life": u(0x42810C + 3), "color": [b(0x427FC4 + 1)] * 3 + [b(0x428005 + 3)],
         "delta": [0, 0, 0, signed(b(0x428012 + 3))], "velocity": [0, f(0x428031 + 3), 0],
         "acceleration": [f(0x4280CA + 1), f(0x4280D2 + 3), f(0x4280CA + 1)],
         "size": 0, "size_min": f(0x428019 + 3), "size_max": f(0x428020 + 3), "growth": f(0x428027 + 3),
         "uv": [f(0x428083 + 2), f(0x42806F + 1), f(0x42806F + 1), f(0x42808F + 3)],
         "columns": 2, "rows": 3, "random_frame": 1},
        {"kind": 23, "constructor": 0x42A180, "cap": -1, "life": u(0x42A299 + 3),
         "color": [b(0x42A1CD + 3), b(0x42A1D1 + 3), 255, 255],
         "delta": [signed(b(address + 3)) for address in (0x42A1D5, 0x42A1DB, 0x42A1E1, 0x42A1E8)],
         "velocity": [0, 0, 0], "acceleration": [0, 0, 0], "size": 0, "size_min": 0, "size_max": 0,
         "growth": f(0x42A1F5 + 3), "uv": [f(0x42A234 + 2), f(0x42A241 + 3), f(0x42A23A + 3), f(0x42A248 + 3)],
         "columns": 1, "rows": 1, "random_frame": 0}
    ]


def encode_effects(definitions):
    data = bytearray(struct.pack("<8s3I", b"EDVFX1\0\0", ATLAS, POOL, len(definitions)))
    for s in definitions:
        data.extend(struct.pack("<IiI4B4b14f3I", s["kind"], s["cap"], s["life"], *s["color"], *s["delta"],
                                *s["velocity"], *s["acceleration"], s["size"], s["size_min"], s["size_max"],
                                s["growth"], *s["uv"], s["columns"], s["rows"], s["random_frame"]))
    return bytes(data)


class SourceRandom:
    """Independent source418E70/418F20 oracle; table and single calls share seed."""
    def __init__(self, seed=12345):
        self.seed, self.index, self.ready, self.table = seed, 0, False, [0] * 64
        self.calls = []

    def _step(self):
        self.seed = (self.seed * 0x41C64E6D + 0x3039) & 0xFFFFFFFF
        return ((((self.seed * 0x88888889) >> 32) >> 3) % 0x07FFFFFF)

    def bound(self, count):
        self.calls.append(("single", count))
        return self._step() % abs(count) if count else 0

    def cached(self, count):
        self.calls.append(("cached", count))
        if not count:
            return 0
        if not self.ready:
            self.table = [self._step() for _ in range(64)]
            self.ready = True
        self.index += 1
        if self.index >= 64:
            self.index, self.ready = 0, False
        return self.table[self.index] % abs(count)


def source_frame_requests(game, random, consumer=None):
    from jpc_scene_activation_case import SceneActivationCase
    case = SceneActivationCase(game)
    case.activate_arrival("ak09")
    case.vm.natives[("ed/base/External", "random", "(I)I")] = random.bound
    from ed_triggers import contains
    def inside(actor_type, actor_index, index):
        case.actor(actor_type, actor_index)
        count, offset = struct.unpack_from("<II", case.payload, 8)
        if not 0 <= index < count:
            raise ValueError("Original VFX frame trigger is missing")
        at = offset + index * 72
        shape = case.payload[at + 1]
        if shape not in (0, 1, 2):
            raise ValueError("Unverified VFX frame trigger geometry")
        points = list(struct.iter_unpack("<3f", case.payload[at + 4:at + 4 + (2 if shape == 0 else shape + 2) * 12]))
        return int(contains({"shape": shape, "points": points}, case.position))
    case.vm.natives[("ed/base/External", "actorInTrigger", "(III)I")] = inside
    requests = []
    def emit(*args):
        requests.append(args)
        if consumer:
            consumer(args, case.scene_data["markers"])
    case.vm.natives[("ed/base/External", "vfxSpawnMarkerOffset", "(IIIIIII)V")] = emit
    case.vm.invoke(2051, case.scene)  # Complete original SceneAK09.processFrame, no selected-instruction replay.
    return requests


def _f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


class ReferenceParticles:
    """Independent Python source-update oracle used only for exported goldens."""
    def __init__(self, definitions, random):
        self.definitions = {d["kind"]: d for d in definitions}
        self.random, self.particles, self.serial = random, {}, 0

    def spawn(self, kind, point):
        d = self.definitions[kind]
        self.serial += 1
        size = d["size"] if d["size"] else _f32(self.random.bound(32767) * _f32(1 / 32767) * (d["size_max"] - d["size_min"]) + d["size_min"])
        tile = self.random.cached(d["columns"] * d["rows"]) if d["random_frame"] else 0
        particle = {"kind": kind, "serial": self.serial, "position": list(point), "velocity": list(d["velocity"]),
                    "size": size, "color": list(d["color"]), "age": 0, "tile": tile}
        self.particles.setdefault(kind, []).insert(0, particle)

    def request(self, args, markers):
        kind, first, second, x, y, z, mode = args
        if mode != 0:
            raise ValueError("Unverified reference VFX render mode")
        point = [_f32(value + offset * _f32(.01)) for value, offset in zip(markers[first], (x, y, z))]
        self.spawn(kind, point)

    def update(self):
        for kind, particles in sorted(self.particles.items()):
            d = self.definitions[kind]
            for p in particles:
                p["velocity"] = [_f32(x + a) for x, a in zip(p["velocity"], d["acceleration"])]
                p["position"] = [_f32(x + v) for x, v in zip(p["position"], p["velocity"])]
                if p["age"] > d["life"]:
                    p["age"] = -1
        for kind, particles in sorted(self.particles.items()):
            d = self.definitions[kind]
            self.particles[kind] = [p for p in particles if p["age"] >= 0]
            for p in self.particles[kind]:
                p["color"] = [min(255, max(0, value + delta)) for value, delta in zip(p["color"], d["delta"])]
                p["age"] += 1
                p["size"] = _f32(p["size"] + d["growth"])

    def flat(self):
        return [p for kind, particles in sorted(self.particles.items()) for p in particles]


def _goldens(game, definitions):
    from jpc_scene_activation_case import SceneActivationCase
    random = SourceRandom()
    reference = ReferenceParticles(definitions, random)
    requests = source_frame_requests(game, random, reference.request)
    ak01 = SceneActivationCase(game)
    ak01.activate_arrival("ak01")
    point = ak01.scene_data["markers"][37]
    reference.spawn(23, point)
    data = bytearray(struct.pack("<8sI", b"EDVFXG1\0", len(requests)))
    for request in requests:
        data.extend(struct.pack("<7i", *request))
    data.extend(struct.pack("<3f3I64I", *point, random.seed, random.index, int(random.ready), *random.table))
    data.extend(struct.pack("<I", 44))
    for tick in range(44):
        particles = reference.flat()
        data.extend(struct.pack("<2I", tick, len(particles)))
        for p in particles:
            data.extend(struct.pack("<IQ7fiI4B", p["kind"], p["serial"], *p["position"], *p["velocity"], p["size"], p["age"], p["tile"], *p["color"]))
        reference.update()
    return bytes(data)


def export_runtime_vfx(game, output):
    game, output = Path(game).resolve(), Path(output).resolve()
    if output==game or game in output.parents:
        raise ValueError("VFX exports must stay outside original game directory")
    definitions = parse_effects((game / "EvilDead.exe").read_bytes())
    target = output / "vfx"
    if output not in target.resolve().parents:raise ValueError("VFX output escapes generated assets")
    target.mkdir(parents=True, exist_ok=True)
    (target / "effects.bin").write_bytes(encode_effects(definitions))
    (target / "state-goldens.bin").write_bytes(_goldens(game, definitions))
    from ed_vfx_hits import parse_hit_effects, encode_hit_effects, hit_goldens
    hits = parse_hit_effects((game / "EvilDead.exe").read_bytes())
    (target / "hit-effects.bin").write_bytes(encode_hit_effects(hits))
    (target / "hit-goldens.bin").write_bytes(hit_goldens(game, hits))
    from ed_vfx_emitters import parse_emitter_effects, encode_emitter_effects, emitter_goldens
    emitters = parse_emitter_effects((game / "EvilDead.exe").read_bytes())
    (target / "emitter-effects.bin").write_bytes(encode_emitter_effects(emitters))
    (target / "emitter-goldens.bin").write_bytes(emitter_goldens(game, emitters))
    from ed_vfx_ranged import export_ranged_effects
    ranged = export_ranged_effects(game, output)
    from ed_vfx_tree import export_tree_effects
    tree = export_tree_effects(game,output)
    from ed_vfx_ash import export_ash_effects
    ash = export_ash_effects(game,output)
    from ed_vfx_variants import export_variant_effects
    variants = export_variant_effects(game,output)
    from ed_vfx_npc import export_npc_effects
    npc = export_npc_effects(game,output)
    archive = (game / "data/az01.pc").read_bytes()
    entry = next(e for e in parse_archive(archive) if e.resource_id == ATLAS)
    texture, name = decode_texture(archive[entry.offset:entry.offset + entry.size])
    (target / "atlas.rgba").write_bytes(encode_texture(ATLAS, texture))
    texture.save(target / "atlas.bmp")
    manifest = {"texture": ATLAS, "texture_name": name, "pool": POOL, "definitions": definitions,
                "scope": "Original episode markers9/23,hit2/recursive18/rotating19,chainsaw0,bonus-birth25/mode3,line7/trail22,Ash flash6/shell8/combat20 reachableTree15/25variants0/2/full24, and original24/25 mode1 with real children3/32 and explicit RGB compatibility repair; decoded NPC16 jitter/dualbillboard,17 flat glow and21 directional blood.",
                "hit_definitions": hits,
                "emitter_definitions": emitters,
                "ranged_definitions": ranged,
                "tree_definitions":tree["definitions"],"tree":tree,
                "ash_definitions":ash["definitions"],"ash":ash,
                "variant_definitions":variants["definitions"],"variants":variants,
                "npc_definitions":npc["definitions"],"npc":npc,
                "rng": "Emitter size uses418EC0 singleRange(0,32767), even when size_min==size_max. Random atlas frame uses418F20 cachedBound with shared418FC0 table/index.",
                "update": "40CE9E after VM4325E0; groups ascending modifier pass, then all groups bookkeeping. Child updates follow group order;15children0 skipalreadyprocessed0 modifiers. Visible24renderchild22 becomesdrawable nextsourceframe.",
                "render": "427DB0 uses original0816E044 atlas, renderstate7 SRCALPHA/ONE additive, depthdisabled and category8 visibility.7=426680 crossedtriangles;22=426CB0 rawfloatcoarse-gridXZdecal; ordinary425640 billboards scale size*.001."}
    (target / "source.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = export_runtime_vfx(args.game, args.output)
    print(json.dumps({"effects": sorted(d["kind"] for field in ("definitions","hit_definitions","emitter_definitions","ranged_definitions","tree_definitions","ash_definitions","variant_definitions","npc_definitions") for d in result[field]), "atlas": result["texture_name"]}))


if __name__ == "__main__":
    main()
