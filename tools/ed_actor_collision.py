"""Bounded deterministic PC collision kernels; not a replacement physics engine.

The primary query is deterministic. Actor403060's second-pass dynamic normal
uses uninitialized PC stack bytes, so this module never invents its final pose.
"""
from dataclasses import dataclass
import math
import struct


def f32(value):
    value = struct.unpack("<f", struct.pack("<f", value))[0]
    if not math.isfinite(value):
        raise ValueError("Non-finite native motion input")
    return value


def vector(values):
    if len(values) != 3:
        raise ValueError("Native motion needs XYZ")
    return tuple(f32(value) for value in values)


def radius_cm(value):
    """403800 FILD int * float45D23C, then store tofloat."""
    if not -(2**31) <= value < 2**31:
        raise ValueError("Native radius argument exceeds int32")
    return f32(value * f32(.01))


def horizontal_query_required(delta):
    delta = vector(delta)
    return delta[0] != 0 or delta[2] != 0


def zero_horizontal_step(local, delta, cached_floor):
    local, delta = vector(local), vector(delta)
    if horizontal_query_required(delta):
        raise ValueError("Only the source zero-XZ path is modeled here")
    result = (local[0], f32(local[1] + delta[1]), local[2])
    return {"local": result, "world_y": f32(result[1] + f32(cached_floor)), "contact_bits": 0}


def static_class_accept(flags, collision_class, enabled=1, shape=0):
    """42B483..4BB classfilter;419F50 has no line handler for shapes4/5."""
    if not enabled or shape not in (0, 1, 2, 3):
        return False
    flags &= 0xffffffff
    if flags & 0xe == 0xe:
        return True
    return bool(flags & {1: 8, 2: 4, 4: 2}.get(collision_class, 0)) if collision_class in (1, 2, 4) else True


@dataclass(frozen=True)
class Collider:
    slot: int
    actor_type: int
    index: int
    local: tuple
    radius_cm: int
    flags: int = 1
    query_mask: int = 0x1e
    aux_state: int = 0


def first_actor_contact(moving, delta, candidates):
    """42B610 primary contact4 only; no invented push/separation/response."""
    origin, delta = vector(moving.local), vector(delta)
    own_radius = radius_cm(moving.radius_cm)
    if not horizontal_query_required(delta) or own_radius <= 0 or not moving.query_mask & 4:
        return None
    slots = set()
    ordered = []
    for target in candidates:
        if target.slot < -1 or target.slot >= 13:
            raise ValueError("Actor registration exceeds source13")
        if target.slot < 0:
            continue
        if target.slot in slots:
            raise ValueError("Duplicate source actor registration")
        slots.add(target.slot)
        ordered.append(target)
    for target in sorted(ordered, key=lambda actor: actor.slot):
        radius = radius_cm(target.radius_cm)
        if ((target.actor_type, target.index) == (moving.actor_type, moving.index)
                or target.flags & 8 or not target.flags & 1 or target.aux_state != 0
                or radius <= 0 or not target.query_mask & 4):
            continue
        point = vector(target.local)
        dx, dz = point[0] - origin[0], point[2] - origin[2]
        squared = dx * dx + dz * dz
        if squared >= (own_radius + radius) ** 2:
            continue  # Tests CURRENT overlap, not a swept/new-position circle.
        # Native retains dx in x87 but also stores it tofloat for this second
        # expression; current squared was separately cached tofloat at42B6A5.
        next_squared = (f32(dx) - delta[0]) ** 2 + (dz - delta[2]) ** 2
        if next_squared <= f32(squared):
            return {"slot": target.slot, "type": target.actor_type, "index": target.index, "contact_bits": 4}
    return None
