"""Source combat VFX definitions; kept separate until the native closure is integrated."""

import hashlib
import struct
from ed_actions import EXE_SHA256, _va_reader


def parse_hit_effects(exe):
    if hashlib.sha256(exe).hexdigest() != EXE_SHA256:
        raise ValueError("Combat VFX requires the verified original PC executable")
    read = _va_reader(exe)
    f = lambda va: struct.unpack("<f", read(va, 4))[0]
    u = lambda va: struct.unpack("<I", read(va, 4))[0]
    b = lambda va: read(va, 1)[0]
    signed = lambda v: v - 256 if v > 127 else v
    common = {"size": 0, "size_min": 0, "size_max": 0, "growth": 0, "velocity": [0, 0, 0],
              "dispersion": [0, 0, 0], "acceleration": [0, 0, 0], "color": [255] * 4, "delta": [0] * 4,
              "columns": 1, "rows": 1, "random_frame": 1, "frame_mode": 0, "frame_delay": 0,
              "count_min": 1, "count_max": 1, "collision": 0, "child_death": 0, "rotation_step": [0] * 3,
              "bounce_percent": [100] * 3, "direction_scale": 0}
    return [
        {**common, "kind": 2, "constructor": 0x428870, "cap": -1, "life": 0,
         "count_min": u(0x41216E + 3), "count_max": u(0x412178 + 3), "direction_scale": .5,
         "dispersion": [f(0x428896 + 1), f(0x428908 + 3), f(0x428896 + 1)],
         "size_min": f(0x4288EA + 3), "size_max": f(0x4288F1 + 3), "growth": f(0x4288F8 + 3),
         "delta": [0, 0, 0, signed(b(0x4288E3 + 3))], "acceleration": [0, f(0x42899A + 3), 0],
         "uv": [f(0x42894E + 2), 0, f(0x428954 + 3), f(0x42895E + 3)],
         "columns": 4, "rows": 8, "frame_mode": 2, "frame_delay": u(0x42893A + 1),
         "collision": 1, "child_death": b(0x4289F4 + 3)},
        {**common, "kind": 18, "constructor": 0x429100, "cap": 200, "life": u(0x429238 + 3),
         "color": [b(0x429129 + 1), b(0x429132 + 1), b(0x429132 + 1), b(0x429129 + 1)],
         "delta": [0, 0, 0, signed(b(0x42916F + 3))],
         "size_min": f(0x429176 + 3), "size_max": f(0x42917D + 3), "growth": f(0x429184 + 3),
         "uv": [f(0x4291D7 + 2), 0, f(0x4291E0 + 3), f(0x4291E7 + 3)], "columns": 4, "rows": 8},
        {**common, "kind": 19, "constructor": 0x428440, "cap": -1, "life": u(0x4285E4 + 3),
         "count_min": u(0x412205 + 3), "count_max": u(0x41220F + 3), "direction_scale": 1,
         "dispersion": [f(0x428469 + 1)] * 3, "size": f(0x4284BF + 3),
         "acceleration": [0, f(0x42855C + 3), 0],
         "uv": [f(0x42850C + 2), f(0x428519 + 3), f(0x428512 + 3), f(0x428520 + 3)], "columns": 2, "rows": 2,
         "collision": 2, "rotation_step": [b(0x42857E + 3), b(0x428582 + 3), b(0x428586 + 3)],
         "bounce_percent": [f(0x4285B1 + 1), f(0x4285C5 + 3), f(0x4285B1 + 1)]}
    ]


def encode_hit_effects(definitions):
    from ed_vfx import encode_effects
    data = bytearray(struct.pack("<8sI", b"EDVFXH1\0", len(definitions)))
    for d in definitions:
        data.extend(encode_effects([d])[20:])
        data.extend(struct.pack("<4f6I4B3f", *d["dispersion"], d["direction_scale"],
                                d["count_min"], d["count_max"], d["frame_mode"], d["frame_delay"],
                                d["collision"], d["child_death"], *d["rotation_step"], d.get("bounce_flags",0), *d["bounce_percent"]))
    return bytes(data)


def source_hit_requests(game, consumer=None):
    """Run the original ActorDB649 -> Deadite1580 -> EDBase159 event path."""
    from pathlib import Path
    from ed_scripts import ScriptPackage
    from jpc_npc_death_case import NpcDeathCase
    case = NpcDeathCase(ScriptPackage((Path(game) / "data/script/pc/levelA.jpc").read_bytes()), health=100)
    requests = []
    def emit(*args):
        requests.append(args)
        if consumer:
            consumer(args)
    case.vm.natives[("ed/base/External", "vfxSpawnActorJoint", "(IIIIIII)V")] = emit
    if case.hit() != 92:
        raise ValueError("Original Axe/Deadite contact path changed")
    return requests


class ReferenceHits:
    """Independent source modifier order, shared pool and emission-RNG oracle."""
    def __init__(self, definitions, random, coarse=lambda x, z: 0, floor=lambda x, y, z: 0):
        self.definitions = {d["kind"]: d for d in definitions}
        self.random, self.coarse, self.floor = random, coarse, floor
        self.active, self.serial = {}, 0
        self.timers = {d["kind"]: d.get("frame_initial",d["frame_delay"]) for d in definitions}
        self.coarse_calls = self.floor_calls = 0

    def spawn(self, kind, point, endpoint=None):
        from ed_vfx import _f32 as f
        d = self.definitions[kind]
        particles = self.active.setdefault(kind, [])
        if d["cap"] >= 0 and len(particles) >= d["cap"]:
            return False
        amount = d["count_min"]
        if amount != d["count_max"]:
            amount += self.random.bound(d["count_max"] - amount)
        endpoint = point if endpoint is None else endpoint
        velocity = [f(base+(b-a)*d["direction_scale"]) for base,a,b in zip(d["velocity"],point,endpoint)]
        emitted = False
        for _ in range(amount):
            if sum(len(group) for group in self.active.values()) >= 1500:
                continue
            p = {"kind": kind, "position": list(point), "velocity": list(velocity), "color": list(d["color"]),
                 "age": 0, "tile": 0, "rotation": [0]*3, "stopped": False}
            for axis in range(3):
                if d["dispersion"][axis]:
                    p["velocity"][axis] = f((self.random.bound(32767) * f(1/32767) - .5) * d["dispersion"][axis] + velocity[axis])
            p["size"] = d["size"] if d["size"] else f(self.random.bound(32767) * f(1/32767) * (d["size_max"]-d["size_min"]) + d["size_min"])
            p["tile"] = self.random.cached(d["columns"]*d["rows"]) if d["random_frame"] else 0
            self.serial += 1
            p["serial"] = self.serial
            particles.insert(0, p)
            emitted = True
        return emitted

    def update(self):
        from ed_vfx import _f32 as f
        # A source group created by a lower-numbered modifier is processed this same tick.
        for kind in sorted(self.definitions):
            d, particles = self.definitions[kind], self.active.get(kind, [])
            if d["frame_mode"]:
                if self.timers[kind]:
                    self.timers[kind] -= 1
                else:
                    self.timers[kind] = d["frame_delay"]
                    for p in particles:
                        p["tile"] = (p["tile"]+1) % (d["columns"]*d["rows"])
            for p in particles:
                p["velocity"] = [f(v+a) for v, a in zip(p["velocity"], d["acceleration"])]
                p["rotation"] = [(v+a) & 255 for v, a in zip(p["rotation"], d["rotation_step"])]
                if not p["stopped"]:
                    p["position"] = [f(x+v) for x, v in zip(p["position"], p["velocity"])]
            if d["collision"]:
                for p in particles:
                    x, y, z = p["position"]
                    self.coarse_calls += 1
                    coarse = self.coarse(x, z)
                    if d["collision"] == 1:
                        if y < coarse or (coarse == 32767 and y < -100):
                            self.floor_calls += 1
                            p["position"][1] = f(self.floor(x, coarse, z))
                            p["age"] = -1
                    elif coarse != 32767 and y < coarse:
                        self.floor_calls += 1
                        value = int(self.floor(x, 32767, z)) & 65535
                        floor = value-65536 if value >= 32768 else value
                        if floor == 32767:
                            p["age"] = -1
                        elif floor > y:
                            p["position"][1] = float(floor)
                            p["velocity"][1] = -p["velocity"][1]
                            if f(-.01) < p["velocity"][1] < f(.01):
                                p["velocity"][1] = 0
                                if not d.get("bounce_flags",0):p["stopped"] = True
                            for axis, percent in enumerate(d["bounce_percent"]):
                                if percent != 100:
                                    value = p["velocity"][axis] * percent * f(.01)
                                    p["velocity"][axis] = 0 if axis != 1 and f(-.01) < value < f(.01) else f(value)
            if d.get("position_jitter"):
                for p in particles:
                    if p["age"]>=d["jitter_min_age"]:
                        for axis,span in enumerate(d["position_jitter"]):
                            p["position"][axis]=f(self.random.bound(32767)*f(1/32767)*span-f(span*.5)+p["position"][axis])
            if d["life"]:
                for p in particles:
                    if p["age"] > d["life"]:
                        p["age"] = -1
            for child in d.get("children",[]):
                for p in particles:
                    if child["flags"]&1:
                        if p["age"]==-1:self.spawn(child["kind"],p["position"])
                    elif child["flags"]&4 and self.random.cached(100)<child["chance"]:
                        self.spawn(child["kind"],p["position"])
            if d["child_death"]:
                for p in particles:
                    if p["age"] == -1:
                        self.spawn(d["child_death"], p["position"])
            if d.get("random_child_chance"):
                for p in particles:
                    if self.random.cached(100)<d["random_child_chance"]:
                        self.spawn(d["random_child"],p["position"])
        for kind, particles in self.active.items():
            d = self.definitions[kind]
            self.active[kind] = [p for p in particles if p["age"] >= 0]
            for p in self.active[kind]:
                p["color"] = [min(255,max(0,c+delta)) for c,delta in zip(p["color"],d["delta"])]
                p["age"] = ((p["age"]+1+32768) & 65535)-32768
                p["size"] = f(p["size"]+d["growth"])

    def flat(self):
        return [p for kind, group in sorted(self.active.items()) for p in group]


def hit_goldens(game, definitions):
    from ed_vfx import SourceRandom
    random = SourceRandom()
    oracle = ReferenceHits(definitions, random)
    joint = (1.0, 1.0, 2.0)  # Geometry fixture; original actor joint transform has separate source golden tests.
    requests = source_hit_requests(game, lambda args: oracle.spawn(args[0], joint))
    # Kind19 is a verified native constructor/emitter fixture; Deadite4 uses kind2, not19.
    oracle.spawn(19, (.25, .8, .75))
    data = bytearray(struct.pack("<8sI", b"EDVFXJ1\0", len(requests)))
    for args in requests:
        data.extend(struct.pack("<7i", *args))
    data.extend(struct.pack("<3f", *joint))
    data.extend(struct.pack("<I", 305))
    for tick in range(305):
        if tick:
            oracle.update()
        data.extend(struct.pack("<7I64I", tick,len(oracle.flat()),random.seed,random.index,int(random.ready),oracle.coarse_calls,oracle.floor_calls,*random.table))
        for p in oracle.flat():
            data.extend(struct.pack("<IQ7fiI4B3BB",p["kind"],p["serial"],*p["position"],*p["velocity"],p["size"],p["age"],p["tile"],*p["color"],*p["rotation"],int(p["stopped"])))
    return bytes(data)
