"""Read action states from the verified original SuperH-4 Dreamcast executable."""

import hashlib

from ed_actions import ACTORS, _parse_actor_actions
from ed_assets import parse_dc_archive


EXECUTABLE_SHA256 = "de26cba0d4e6035d1128daf504a2257a695050bac11ecdf6e9e2e0baf0f26841"
IMAGE_BASE = 0x8C010000
TABLES = {
    "ash": 0x8C0CD98C, "deadite": 0x8C0C8150, "skullbat": 0x8C0CB550,
    "skeleton": 0x8C0CB7EC, "hellbillyfat": 0x8C0C8A3C, "hellbillythin": 0x8C0C8D44,
    "demonpig": 0x8C0CADD0, "headlessskeleton": 0x8C0C76E0, "troopknife": 0x8C0C9C9C,
    "troopclaw": 0x8C0C93BC, "trooprange": 0x8C0C90C0, "treeboss": 0x8C0CBFE4,
    "treearm": 0x8C0CBF44, "troopleaderboss": 0x8C0CBD04, "matriarchboss": 0x8C0C99D0,
    "deadash": 0x8C0CED30, "anniezombie": 0x8C0C7360, "badash": 0x8C0CF4A4,
    "guildmember": 0x8C0C85BC, "merchant": 0x8C0C9EFC, "wizardboss": 0x8C0CC574,
    "mudgolem": 0x8C0C96A4, "armoryskeleton": 0x8C0CB138, "armoryboss": 0x8C0C70EC,
    "netherpet": 0x8C0CA454, "cthulhu": 0x8C0C7EC0, "captain": 0x8C0C7B40,
    "pumpkeeper": 0x8C0CF1F4,
}


def read_image(data, address, size):
    offset = address - IMAGE_BASE
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ValueError(f"Unmapped Dreamcast image range {address:08x}/{size}")
    return data[offset:offset + size]


def parse_dc_actor_actions(executable, archive, actor="ash"):
    if hashlib.sha256(executable).hexdigest() != EXECUTABLE_SHA256:
        raise ValueError("Dreamcast action tables require the verified original 1ST_READ.BIN")
    if actor not in TABLES:
        raise ValueError("Unverified Dreamcast actor action table")
    _, count, resources = ACTORS[actor]
    return _parse_actor_actions(lambda address, size: read_image(executable, address, size),
                               archive, actor, TABLES[actor], count, resources, parse_dc_archive)
