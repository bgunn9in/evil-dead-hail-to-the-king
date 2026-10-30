"""Decode Ash's original EXE state table and model its bounded native actions.

Times below are source animation updates. The PC frame pacing is not yet
established, so this module deliberately does not assign seconds to them.
"""

from dataclasses import dataclass, field
import hashlib
import struct

from ed_assets import parse_archive
from ed_model import decode_animations


EXE_SHA256 = "62ae3c5512fb164e64f2c4375b67ae8c38f7c0da8219f4b41f8aa9b3a4ca862f"
ASH_TABLE_VA = 0x46BAD0
ASH_STATE_COUNT = 163
ACTORS = {
    "ash": (ASH_TABLE_VA, ASH_STATE_COUNT, (0x01024000, 0x01824184)),
    "deadite": (0x4658B8, 30, (0x050C6000, 0x058C6284)),
    "skullbat": (0x469000, 9, (0x05482000, 0x05C82F04)),
    "skeleton": (0x4692A0, 37, (0x07494000, 0x07C940C4)),
    "hellbillyfat": (0x4661B0, 17, (0x071CA000, 0x079CA0C4)),
    "hellbillythin": (0x4664B8, 27, (0x071D8000, 0x079D8284)),
    "demonpig": (0x468558, 17, (0x073CC000, 0x07BCC284)),
    "headlessskeleton": (0x464E48, 30, (0x07082000, 0x078820C4)),
    "troopknife": (0x467420, 23, (0x07318000, 0x07B18504)),
    "troopclaw": (0x466B38, 23, (0x07304000, 0x07B04504)),
    "trooprange": (0x466838, 21, (0x07300000, 0x07B00504)),
    "treeboss": (0x469A98, 6, (0x034E2000, 0x03CE2284)),
    "treearm": (0x4699F8, 3, (0x034C0000, 0x03CC0F04)),
    "troopleaderboss": (0x4697B8, 22, (0x03496000, 0x03C960C4)),
    "matriarchboss": (0x467150, 19, (0x03310000, 0x03B10144)),
    "deadash": (0x46D1E8, 31, (0x030C0000, 0x038C0284)),
    "anniezombie": (0x4647E8, 22, (0x03032000, 0x03832284)),
    "badash": (0x464AE0, 21, (0x07040000, 0x078400C4)),
    "guildmember": (0x465D28, 28, (0x07198000, 0x07998A04)),
    "merchant": (0x467680, 4, (0x07326000, 0x07B26144)),
    "wizardboss": (0x46A030, 15, (0x035B2000, 0x03DB2784)),
    "mudgolem": (0x466E20, 17, (0x0330C000, 0x03B0C0C4)),
    "armoryskeleton": (0x468BE8, 34, (0x07480000, 0x07C800C4)),
    "armoryboss": (0x464570, 13, (0x03002000, 0x03802504)),
    "netherpet": (0x467BD8, 18, (0x0735E000, 0x07B5E0C4, 0x07B620C4)),
    "cthulhu": (0x465628, 7, (0x030A8000, 0x038A8284)),
    "captain": (0x4652A8, 30, (0x0708C000, 0x0788C0C4)),
    "pumpkeeper": (0x468850, 19, (0x073D4000, 0x07BD40C4)),
}


def _va_reader(exe):
    def unpack(fmt, offset):
        if offset < 0 or offset > len(exe) - struct.calcsize(fmt):
            raise ValueError("PE structure outside file bounds")
        return struct.unpack_from(fmt, exe, offset)
    if len(exe) < 64 or exe[:2] != b"MZ":
        raise ValueError("Missing PE DOS header")
    pe = unpack("<I", 60)[0]
    if exe[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Missing PE signature")
    count, optional_size = unpack("<H", pe + 6)[0], unpack("<H", pe + 20)[0]
    if not 1 <= count <= 96 or unpack("<H", pe + 24)[0] != 0x10B:
        raise ValueError("Unsupported PE section count or format")
    base = unpack("<I", pe + 52)[0]
    sections = []
    for index in range(count):
        at = pe + 24 + optional_size + index * 40
        _, rva, size, offset = unpack("<4I", at + 8)
        if offset > len(exe) or size > len(exe) - offset:
            raise ValueError("PE section outside file bounds")
        sections.append((base + rva, size, offset))
    def read(address, size):
        for va, length, offset in sections:
            relative = address - va
            if 0 <= relative <= length and 0 <= size <= length - relative:
                return exe[offset + relative:offset + relative + size]
        raise ValueError(f"Unmapped PE address0x{address:08x}")
    return read


def parse_actor_actions(exe: bytes, archive: bytes, actor="ash") -> list[dict]:
    if hashlib.sha256(exe).hexdigest() != EXE_SHA256:
        raise ValueError("Actor action tables are only verified for the recorded original PC EXE")
    if actor not in ACTORS:
        raise ValueError("Unverified original actor action table")
    table, count, resources = ACTORS[actor]
    return _parse_actor_actions(_va_reader(exe), archive, actor, table, count, resources, parse_archive)


def _parse_actor_actions(read, archive, actor, table, count, resources, parse_entries):
    clips = {}
    for entry in parse_entries(archive):
        if entry.resource_id not in resources:
            continue
        payload = archive[entry.offset:entry.offset + entry.size]
        decoded = decode_animations(payload)
        offsets = struct.unpack_from(f"<{len(decoded)}I", payload, 4)
        for clip, start in zip(decoded, offsets):
            timeline = struct.unpack_from("<6H", payload, start + 8)
            clips[clip["resource_id"]] = {**clip, "loop_start": timeline[0], "loop_end": timeline[1],
                "start": timeline[2], "early_end": timeline[3], "finish_start": timeline[4], "end": timeline[5]}
    states = []
    for index in range(count):
        animation, mode, mask, callback, begin, end, size, transitions = struct.unpack(
            "<IIIhBBII", read(table + index * 24, 24))
        if mode not in (0, 1) or callback < 0 or callback > 10 or size % 4 or size > 4096:
            raise ValueError("Unsupported source Ash action state")
        clip = clips.get(animation)
        if animation and clip is None:
            raise ValueError(f"Missing original {actor} animation0x{animation:08x}")
        rules = [list(struct.unpack("<BBH", read(transitions + at, 4))) for at in range(0, size, 4)]
        if any(flags & ~7 or target not in (0xFFFE, 0xFFFF) and target >= count
               for _, flags, target in rules):
            raise ValueError("Invalid source Ash state transition")
        bones = clip["bone_count"] if clip else 17
        bone_mask = list(read(mask, bones)) if mask else []
        states.append({"index": index, "source_va": table + index * 24,
            "animation_id": animation, "playback_mode": mode, "callback_id": callback,
            "begin_event": begin, "end_event": end, "bone_mask": bone_mask, "transitions": rules,
            "clip_flags": clip["flags_raw"] if clip else 0,
            "frame_count": clip["frame_count"] if clip else 0, "bone_count": bones,
            **{name: clip[name] if clip else 0 for name in ("start", "end", "loop_start", "loop_end", "early_end", "finish_start")}})
    return states


def parse_ash_actions(exe: bytes, archive: bytes) -> list[dict]:
    return parse_actor_actions(exe, archive, "ash")


def encode_actions(states):
    if not 1 <= len(states) <= 4096 or any(state["index"] != index for index, state in enumerate(states)):
        raise ValueError("Invalid original actor state count or order")
    output = bytearray(b"EDACT1\0\0" + struct.pack("<I", len(states)))
    for state in states:
        output.extend(struct.pack("<IIhBBI8HII", state["animation_id"], state["playback_mode"],
            state["callback_id"], state["begin_event"], state["end_event"], state["clip_flags"],
            *[state[name] for name in ("start", "end", "loop_start", "loop_end", "early_end", "finish_start", "frame_count", "bone_count")],
            len(state["bone_mask"]), len(state["transitions"])))
        output.extend(bytes(state["bone_mask"]))
        for rule in state["transitions"]:
            output.extend(struct.pack("<BBH", *rule))
    return bytes(output)


@dataclass
class Layer:
    state: int | None = None
    playback: int | None = None
    history: list = field(default_factory=lambda: [None] * 10)
    history_index: int = 0


@dataclass
class Playback:
    state: int = 0
    frame: int = 0
    mode: int = 0
    desired_mode: int = 0
    step: int = 1
    active: bool = False
    reusable: bool = True
    parent: int | None = None
    child: int | None = None


class NativeActions:
    def __init__(self, states):
        self.states = states
        self.layers = [Layer() for _ in range(7)]
        self.playbacks = [Playback() for _ in range(50)]
        self.events = []
        self.updates = 0
        self.layers[0].state = 0
        self.layers[0].playback = self._create(0, 1, False)

    def _configure(self, index, state_index, mode):
        state = self.states[state_index]
        self.playbacks[index] = Playback(state_index,
            state["loop_start"] if mode == 2 else state["start"], mode, mode, active=True, reusable=False)
        if state["begin_event"]:
            self.events.append([self.updates, state["begin_event"], index, state_index])

    def _create(self, state_index, mode, priority):
        if not self.states[state_index]["animation_id"]:
            return None
        start = 0
        if priority:
            occupied = [i for i in range(25, 50) if self.playbacks[i].active]
            start = max(occupied) + 1 if occupied else 25
        free = next((i for i in range(start, 50) if not self.playbacks[i].active and self.playbacks[i].reusable), None)
        if free is None:
            raise ValueError("Original50-record animation pool is full")
        self._configure(free, state_index, mode)
        return free

    def _overwrite(self, index, state_index, mode):
        previous = self.playbacks[index]
        event = self.states[previous.state]["end_event"]
        if previous.active and event:
            self.events.append([self.updates, event, index, previous.state])
        self._configure(index, state_index, mode)
        self.playbacks[index].parent = previous.parent #4058E0 preserves+04.

    def _replace(self, slot, target, immediate=False):
        layer, state = self.layers[slot], self.states[target]
        old = self.playbacks[layer.playback] if layer.playback is not None else None
        if old is None or not old.active:
            layer.playback = self._create(target, state["playback_mode"], False)
        elif immediate:
            self._overwrite(layer.playback, target, state["playback_mode"])
        elif state["animation_id"]:
            index = self._create(target, 4, False)
            old.child = index
            child = self.playbacks[index]
            child.parent, child.desired_mode = layer.playback, state["playback_mode"]
            previous = self.states[old.state]
            if previous["clip_flags"] & 4 and old.frame <= previous["early_end"]:
                old.mode = 6
            else:
                old.frame, old.mode = previous["finish_start"], 3
                old.step = 1 if previous["end"] >= old.frame else -1
            layer.playback = index
        layer.state = target

    def _disable(self, layer):
        layer.state = None
        if layer.playback is not None:
            self.playbacks[layer.playback].active = False
            self.playbacks[layer.playback].reusable = True

    def possible(self, action):
        for layer in self.layers:
            if layer.state is None:
                continue
            for code, flags, target in self.states[layer.state]["transitions"]:
                if code == action and target != 0xFFFE:
                    return not flags & 1 or any(item.state is None for item in self.layers)
        return False

    def command(self, action):
        accepted = False
        for slot, layer in enumerate(self.layers):
            if layer.state is None:
                continue
            rule = next((rule for rule in self.states[layer.state]["transitions"] if rule[0] == action), None)
            if rule is None:
                continue
            _, flags, target = rule
            if target == 0xFFFE:
                self._disable(layer)
                continue
            accepted = True
            if target == 0xFFFF:
                previous = layer.history[layer.history_index]
                layer.history_index = (layer.history_index - 1) % 10
                if previous is None:
                    layer.state = None
                    if layer.playback is not None:
                        play = self.playbacks[layer.playback]
                        play.mode = 6 if self.states[play.state]["clip_flags"] & 4 else 5
                else:
                    layer.state = previous
                    if layer.playback is not None and self.playbacks[layer.playback].active:
                        self._overwrite(layer.playback, previous, 2)
                    else:
                        layer.playback = self._create(previous, 2, False)
                continue
            if flags & 4 and slot == 0:
                for other in self.layers[1:]:
                    if other.state is not None:
                        self._disable(other) #4187E6 skips inactive historical pointers.
            duplicate = next((index for index, other in enumerate(self.layers)
                              if other.state == target and (flags & 1 or index != slot)), None)
            if duplicate is not None:
                if not flags & 1:
                    layer.state = None
                    if layer.playback is not None:
                        self.playbacks[layer.playback].mode = 3
                continue
            if flags & 1:
                free = [index for index, other in enumerate(self.layers) if other.state is None]
                if not free:
                    return False
                destination = free[-1]
                self.layers[destination].history_index = 0
                self.layers[destination].history[0] = None
                self.layers[destination].state = target
                self.layers[destination].playback = self._create(target, self.states[target]["playback_mode"], True)
            else:
                layer.history_index = (layer.history_index + 1) % 10
                layer.history[layer.history_index] = layer.state
                self._replace(slot, target, bool(flags & 2))
        return accepted

    def tick(self):
        self.updates += 1
        completed = []
        for index, play in enumerate(self.playbacks):
            if not play.active or play.mode == 4:
                continue
            state = self.states[play.state]
            play.frame += play.step
            if play.mode == 6 and play.frame > state["early_end"]:
                play.frame, play.mode = state["finish_start"], 3
                play.step = 1 if state["end"] >= play.frame else -1
            if play.mode == 1 and play.frame >= state["loop_start"]:
                play.mode = 2
                if state["clip_flags"] & 0x100 and state["end_event"]:
                    self.events.append([self.updates, state["end_event"], index, play.state])
            if play.mode == 2:
                if play.frame > state["loop_end"]:
                    play.frame = state["loop_start"]
                continue
            limit = state["early_end"] if state["clip_flags"] & 8 else state["end"]
            finished = play.mode == 5 or (play.mode == 0 and play.frame > limit) or (
                play.mode == 3 and (play.frame > state["end"] if play.step > 0 else play.frame < state["end"]))
            if finished:
                play.active = False
                if state["end_event"]:
                    self.events.append([self.updates, state["end_event"], index, play.state])
                if play.child is not None:
                    child = self.playbacks[play.child]
                    if child.child is not None:
                        child.mode, child.frame = 3, self.states[child.state]["finish_start"]
                    else:
                        child.mode = child.desired_mode
                    play.reusable = True
                else:
                    completed.append(index)
        for index in completed:
            self.playbacks[index].reusable = True
            # Native406560 sends39 once per finished record without a successor.
            self.command(39)


class EventWaits:
    """Native432770's actor/event matching; VM frame suspension belongs in the VM."""
    def __init__(self):
        self.waiting = {}

    def sleep(self, thread, actor_type, actor_index, event):
        if thread in self.waiting:
            raise ValueError("Thread already waits for an event")
        self.waiting[thread] = (actor_type, actor_index, event)

    def deliver(self, actor_type, actor_index, event):
        ready = [thread for thread, (kind, index, code) in self.waiting.items()
                 if (kind < 0 or kind == actor_type) and (index < 0 or index == actor_index) and code == event]
        for thread in ready:
            del self.waiting[thread]
        return ready
