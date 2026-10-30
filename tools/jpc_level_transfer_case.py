"""Prove the original player-only A/B transfer with separate JPC interpreters.

Only integer bytes cross the boundary. The fixture allocates source-shaped Ash,
inventory and weapon graphs, using their original constructors. It supplies the
native actor pose/part registry; serialization and restoration execute JPC.
"""

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path

from ed_scripts import ScriptPackage
from ed_vm import Array, Interpreter, Object, ScriptError


ROOT = Path(__file__).resolve().parents[1]
INVENTORIES = ((99, 12, 80, 258), (98, 5, 2, 256),
               (100, 14, 256, 66), (101, 14, 80, 0))
AMMO_FIELDS = (106, 108, 107, 112, 113, 104, 105, 110, 111)


def native_transfer_policy(current_scene, destination_scene):
    """Exact native predicates at 420E4F/420ECA and 40CC3B; not a host policy."""
    if not all(isinstance(value, int) and 0 <= value <= 0xFFFFFFFF
               for value in (current_scene, destination_scene)):
        raise ValueError("Scene IDs must be unsigned32")
    restart = destination_scene & 0xFFFFFF00 in (0x61610100, 0x62620100)
    reload_package = bool((current_scene ^ destination_scene) & 0xFF000000) or restart
    restore = ((destination_scene == 0x626D0100 and current_scene >> 24 == 0x61)
               or (destination_scene == 0x616B0100 and current_scene == 0x62620100))
    return {"reload_package": reload_package, "clear_transfer_buffer": restart,
            "save_player": reload_package and not restart,
            "restore_player": reload_package and restore,
            "package": "levelA.jpc" if destination_scene >> 24 == 0x61 else "levelB.jpc"}


def symbol(method):
    return (method["class_name"], method["name"], method["descriptor"])


def normalized_code(package, method):
    """Resolve pool operands; retain anonymous field slots and numeric constants."""
    code = package.disassemble(method["index"])
    labels = {row["offset"]: index for index, row in enumerate(code)}
    result = []
    for row in code:
        item = [row["mnemonic"]]
        if "method" in row:
            item.append(symbol(row["method"]))
        elif "field_index" in row:
            item.append(row["field_index"])
        elif "class_name" in row:
            item.append(row["class_name"])
            if row["opcode"] == 0xC5:
                item.append(row["operands"][1])
        elif "constant" in row:
            constant = row["constant"]
            item.append(["string", package.literal_string(constant["value_raw"])["text"]]
                        if constant["index_raw"] < 0 else ["integer", constant["signed_value"]])
        elif "target" in row:
            item.append(labels[row["target"]])
        elif "cases" in row:
            item += [labels[row["default"]], [[key, labels[target]] for key, target in row["cases"]]]
        else:
            item.extend(row["operands"])
        result.append(item)
    return result


def compare_kernels(a, b):
    def unique_symbols(package):
        groups = defaultdict(list)
        for method in package.methods:
            groups[symbol(method)].append(method)
        return ({key: methods[0] for key, methods in groups.items() if len(methods) == 1},
                [{"symbol": key, "indices": [m["index"] for m in methods]}
                 for key, methods in groups.items() if len(methods) != 1])
    by_a, duplicate_a = unique_symbols(a)
    by_b, duplicate_b = unique_symbols(b)
    common = sorted(by_a.keys() & by_b.keys())
    changed, moved, same = [], [], []
    for key in common:
        left, right = by_a[key], by_b[key]
        if left["index"] != right["index"]:
            moved.append({"symbol": key, "a": left["index"], "b": right["index"]})
        if normalized_code(a, left) == normalized_code(b, right):
            same.append(key)
        else:
            changed.append({"symbol": key, "a": left["index"], "b": right["index"]})
    prefix = 0
    while prefix < min(len(a.methods), len(b.methods)) and symbol(a.methods[prefix]) == symbol(b.methods[prefix]):
        prefix += 1
    common_field_prefix = 0
    while (common_field_prefix < min(len(a.fields_raw), len(b.fields_raw))
           and a.fields_raw[common_field_prefix] == b.fields_raw[common_field_prefix]):
        common_field_prefix += 1
    return {"a_methods": len(a.methods), "b_methods": len(b.methods),
            "common_symbols": len(common), "identical_resolved_bodies": len(same),
            "same_symbol_numeric_prefix": prefix, "moved_symbols": moved,
            "changed_bodies": changed, "a_only_symbols": sorted(by_a.keys() - by_b.keys()),
            "b_only_symbols": sorted(by_b.keys() - by_a.keys()),
            "ambiguous_symbols_excluded": {"A": duplicate_a, "B": duplicate_b},
            "field_records_equal_prefix": common_field_prefix,
            "a_fields": len(a.fields_raw), "b_fields": len(b.fields_raw),
            "field_metadata_limit": "Field names are stripped (CDCD); equality is raw schema, not a symbolic migration map."}


def expected_player_bytes(case):
    """Independent bit layout oracle, transcribed from the source save methods."""
    bits = []

    def put(value, width):
        if not 0 <= value < 1 << width:
            raise ValueError("Fixture value is outside the source serialized range")
        bits.extend((value >> shift) & 1 for shift in range(width - 1, -1, -1))

    ash = case.ash.fields
    put(ash[13], 8)
    for field, width in ((1251, 1), (1269, 1), (1262, 32), (1238, 8), (1270, 1)):
        put(ash[field], width)
    for value in case.pose:
        put(int(value >= 0), 1)
        put(abs(value), 16)
    for field in (99, 101, 98, 100):
        slots = ash[field].fields[996].values
        put(len(slots), 8)
        for slot in slots:
            fields = slot.fields
            for key, width in ((32, 1), (33, 16), (34, 8), (35, 32), (36, 16)):
                put(fields[key], width)
            put(int(fields[37] != -1), 1)
            if fields[37] != -1:
                put(fields[37] & 0xFFFFFFFF, 32)
    for field in (103, 102):
        weapon = ash[field]
        put(int(isinstance(weapon, Object)), 1)
        if isinstance(weapon, Object):
            put(weapon.fields[812], 8)
    for field in AMMO_FIELDS:
        put(max(0, ash[field].fields[815]), 8)
    padded = bits + [0] * (-len(bits) % 8)
    return bytes(sum(padded[index + shift] << (7 - shift) for shift in range(8))
                 for index in range(0, len(padded), 8)), divmod(len(bits), 8)


class PlayerTransferCase:
    def __init__(self, game, package_name):
        data = (Path(game) / f"data/script/pc/level{package_name}.jpc").read_bytes()
        self.package = ScriptPackage(data)
        self.sha256 = hashlib.sha256(data).hexdigest()
        self.vm = Interpreter(self.package, step_limit=1000000)
        self.native_events = []
        self.pose = [0, 0, 0, 0]

        def bind(name, callback):
            index = self.vm.method_index("ed/base/External", name)
            method = self.package.methods[index]
            self.vm.natives[symbol(method)] = callback

        for offset, name in enumerate(("actorGetPositionX", "actorGetPositionY", "actorGetPositionZ", "actorGetRotY")):
            bind(name, lambda actor_type, actor_index, i=offset: self.get_pose(actor_type, actor_index, i))
        bind("actorSetPosition", self.set_position)
        bind("actorFace", self.face)
        bind("actorSetPartVisible", lambda *args: self.native_events.append(["actorSetPartVisible", list(args)]))
        bind("actorDoConsoleAction", lambda *args: self.console_action(*args))
        self.vm.initialize("ed/base/EDBase")
        self.vm.initialize("MissionCommander")
        self.ash = Object(self.vm.class_index("Ash"))
        self.vm.invoke(self.vm.method_index("Ash", "<init>", "()V"), self.ash)
        self.ash.fields.update({5: 0, 6: 0, 13: 100})
        for field, slots, accepted, excluded in INVENTORIES:
            inventory = Object(self.vm.class_index("Inventory"))
            self.vm.invoke(self.vm.method_index("Inventory", "<init>"), inventory,
                           (self.ash, slots, accepted, excluded))
            self.ash.fields[field] = inventory
        # Exact original new/constructor/field sequence Ash.initialize659@161..278.
        code = self.package.disassemble(self.vm.method_index("Ash", "initialize"))
        weapon_class = constructor = None
        for row in code:
            if not 161 <= row["offset"] <= 278:
                continue
            if row["mnemonic"] == "new":
                weapon_class = row["constant"]["value_raw"]
            elif row["mnemonic"] == "invokespecial":
                constructor = row["method"]["index"]
            elif row["mnemonic"] == "putfield":
                weapon = Object(weapon_class)
                self.vm.invoke(constructor, weapon, (self.ash,))
                self.ash.fields[row["field_index"]] = weapon
                weapon_class = constructor = None
        # Native ActorDB pool boundary: fresh script refs, player type0 at base0.
        self.vm.statics.update({53: Array("L", [self.ash]), 54: Array("I", [0])})

    def check_actor(self, actor_type, actor_index):
        if (actor_type, actor_index) != (0, 0):
            raise ScriptError("Transfer fixture only contains the source Ash actor")

    def get_pose(self, actor_type, actor_index, axis):
        self.check_actor(actor_type, actor_index)
        return self.pose[axis]

    def set_position(self, actor_type, actor_index, x, y, z):
        self.check_actor(actor_type, actor_index)
        self.pose[:3] = [x, y, z]
        self.native_events.append(["actorSetPosition", [actor_type, actor_index, x, y, z]])

    def face(self, actor_type, actor_index, heading):
        self.check_actor(actor_type, actor_index)
        self.pose[3] = heading
        self.native_events.append(["actorFace", [actor_type, actor_index, heading]])

    def console_action(self, actor_type, actor_index, action):
        self.check_actor(actor_type, actor_index)
        self.native_events.append(["actorDoConsoleAction", [actor_type, actor_index, action]])
        return 1  # Environmental FSM acceptance only; no host equipment logic.

    def populate(self, equipped):
        self.pose[:] = [123, -45, 678, 137]
        self.ash.fields.update({13: 73, 1251: 1, 1269: 0, 1262: 0x12345678, 1238: 3, 1270: 1})
        for item, quantity in ((29, 1), (119, 2)):
            inventory = self.ash.fields[99]
            self.vm.invoke(self.vm.method_index("Inventory", "addItem", "(II)I"), inventory, (item, quantity))
        for value, field in enumerate(AMMO_FIELDS):
            self.ash.fields[field].fields[815] = value * 7
        if equipped:
            # Source initial equipped refs: Axe and Chainsaw, Ash.initialize659@281..294.
            self.ash.fields[103] = self.ash.fields[109]
            self.ash.fields[102] = self.ash.fields[106]
        self.vm.statics[435] = 1  # A mission flag outside the player transfer graph.

    def state(self):
        return {"health": self.ash.fields[13], "native_pose_cm_degrees": self.pose.copy(),
                "inventories": {str(field): [{str(key): slot.fields.get(key, 0) for key in range(32, 38)}
                                             for slot in self.ash.fields[field].fields[996].values]
                                for field, *_ in INVENTORIES},
                "ammo": [self.ash.fields[field].fields[815] for field in AMMO_FIELDS],
                "equipped_item_ids": [self.ash.fields.get(field).fields[812]
                                      if isinstance(self.ash.fields.get(field), Object) else None for field in (103, 102)],
                "mission_flag435": self.vm.statics.get(435, 0)}

    def save(self):
        buffer = Array("B", [0] * 0x3840)
        count = self.vm.invoke(self.vm.method_index("Main", "savePlayer"), args=(buffer,))
        cursor = self.vm.statics[15]
        used = count + int(cursor != 0)
        return bytes(value & 255 for value in buffer.values), count, cursor, used

    def restore(self, data):
        buffer = Array("B", list(data))
        count = self.vm.invoke(self.vm.method_index("Main", "restorePlayer"), args=(buffer,))
        return count, self.vm.statics[15]


def run_case(game):
    results = []
    for source_name, target_name in (("A", "B"), ("B", "A")):
        for equipped in (False, True):
            source = PlayerTransferCase(game, source_name)
            source.populate(equipped)
            before = source.state()
            expected_bytes, expected_cursor = expected_player_bytes(source)
            data, count, bit, used = source.save()
            if (count, bit) != expected_cursor or data[:used] != expected_bytes:
                raise ValueError("Original serializer disagrees with the independent bit-layout oracle")
            source_methods = sorted({method for method, _ in source.vm.trace})
            target = PlayerTransferCase(game, target_name)
            target_count, target_bit = target.restore(data)
            after = target.state()
            expected = dict(before, mission_flag435=0)
            if after != expected or (target_count, target_bit) != (count, bit):
                raise ValueError("Original cross-package player serialization did not round-trip")
            if target.ash.fields[1262] != -1 or target.ash.fields[1269] != 1:
                raise ValueError("Original GenericNpc restore reset was not executed")
            if target.ash is source.ash or target.ash.fields[99] is source.ash.fields[99]:
                raise ValueError("Fixture accidentally shared references across package boundary")
            results.append({"source": source_name, "destination": target_name, "equipped": equipped,
                            "full_bytes_cursor": count, "partial_bits": bit, "bytes_touched": used,
                            "player_sha256": hashlib.sha256(data[:used]).hexdigest(),
                            "source_methods": source_methods,
                            "target_methods": sorted({method for method, _ in target.vm.trace}),
                            "restored": after, "target_native_events": target.native_events})
    a = PlayerTransferCase(game, "A")
    b = PlayerTransferCase(game, "B")
    index_examples = []
    for owner, name, descriptor in (("Main", "initialize", "()V"), ("Main", "processFrame", "()V"),
                                    ("Main", "createScene", "(II)V"), ("Main", "endScene", "()V"),
                                    ("Main", "createLevel", "(I)V"), ("Main", "savePlayer", "([B)I"),
                                    ("Main", "restorePlayer", "([B)I"), ("Ash", "save", "()V"),
                                    ("GenericNpc", "restore", "()V"), ("Inventory", "save", "()V"),
                                    ("Deadite", "initialize", "()V"), ("SkullBat", "initialize", "()V"),
                                    ("MenSaveLoad", "loadGameEnd", "()V")):
        index_examples.append({"symbol": [owner, name, descriptor],
                               "A": a.vm.method_index(owner, name, descriptor),
                               "B": b.vm.method_index(owner, name, descriptor)})
    return {"source_hashes": {"levelA": a.sha256, "levelB": b.sha256},
            "transfer_cases": results, "kernel_comparison": compare_kernels(a.package, b.package),
            "numeric_index_examples": index_examples,
            "limits": ["Source-shaped bounded player graph, not full game startup.",
                       "A/B directions prove byte compatibility; native entry gates permit only documented scene pairs.",
                       "Native pose/part/FSM registry is environmental. No quest, inventory or equipment predicate is replaced."]}


def full_save_case(game, package_name):
    """Check the distinct tagged full-save stream, including its partial-byte quirk."""
    source = PlayerTransferCase(game, package_name)
    source.populate(True)
    source.ash.fields[111].fields[815] = 63
    tag = int(package_name == "B")
    field, count, size = (1499, 32, 342) if tag == 0 else (1485, 30, 380)
    scenes = Array("L", [Array("B", [(index * 17 + offset + 0xA5) & 255 for offset in range(size)])
                         for index in range(count)])
    source.vm.statics.update({397: tag, field: scenes})
    buffer = Array("B", [0] * 0x3840)
    length = source.vm.invoke(source.vm.method_index("Main", "save"), args=(buffer,))
    target = PlayerTransferCase(game, package_name)
    blank_scenes = Array("L", [Array("B", [0] * size) for _ in range(count)])
    target.vm.statics.update({397: tag, field: blank_scenes})
    restored = target.vm.invoke(target.vm.method_index("Main", "restore"), args=(123, 456, Array("B", buffer.values.copy())))
    if (target.vm.statics[435] != 1
            or [[v & 255 for v in a.values] for a in blank_scenes.values] != [a.values for a in scenes.values]):
        raise ValueError("Full-save quest/scene stream did not restore")
    if restored != length + 1 or target.vm.statics[136] != 123 or target.vm.statics[137] != 456:
        raise ValueError("Unexpected original Main.restore return/argument contract")
    # Main821@70 copies bytes starting at floor(bitCursor/8), with no alignment.
    # The original final 3 player bits are replaced by the first room byte's top3.
    expected_last_ammo = (63 & ~7) | (scenes.values[0].values[0] >> 5)
    if target.ash.fields[111].fields[815] != expected_last_ammo:
        raise ValueError("Original unaligned raw scene-copy behavior changed")
    other = PlayerTransferCase(game, "A" if tag else "B")
    other.vm.statics[397] = 1 - tag
    before = other.state()
    rejected = other.vm.invoke(other.vm.method_index("Main", "restore"), args=(123, 456, Array("B", buffer.values.copy())))
    if rejected != 0 or other.state() != before:
        raise ValueError("Full save incorrectly restored into another package")
    return {"package": package_name, "tag": tag, "save_return_bytes": length,
            "restore_return_bytes": restored, "partial_bit_cursor": source.vm.statics[15],
            "scene_stream_field": field, "scene_stream_count": count, "scene_stream_bytes": size,
            "quest_flag435_restored": True, "opposite_package_rejected": True,
            "unaligned_source_copy": {"original_last_ammo": 63, "first_room_byte": scenes.values[0].values[0],
                                      "restored_last_ammo": expected_last_ammo,
                                      "source_copy_begins_at_full_byte_cursor": True}}


def write_cpp_oracles(game, directory):
    game, directory = Path(game).resolve(), Path(directory).resolve()
    if directory == game or game in directory.parents:
        raise ValueError("Player transfer oracles must be outside the original game directory")
    directory.mkdir(parents=True, exist_ok=True)
    records = []
    for equipped, name in ((False, "unequipped.player"), (True, "equipped.player")):
        case = PlayerTransferCase(game, "A")
        case.populate(equipped)
        expected, cursor = expected_player_bytes(case)
        data, full, bit, used = case.save()
        if (full, bit) != cursor or data[:used] != expected:
            raise ValueError("Cannot publish a player oracle that differs from the independent bit schema")
        (directory / name).write_bytes(expected)
        records.append({"file": name, "full_bytes": full, "partial_bits": bit,
                        "bytes_touched": used, "sha256": hashlib.sha256(expected).hexdigest()})
    result = {"format": "ED-PLAYER-TRANSFER-ORACLE1", "source_levelA_sha256": case.sha256, "cases": records,
              "scope": "Bounded source-constructed player fixture; byte layout generated by original Main814 and independently checked."}
    (directory / "source.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--oracle-dir", type=Path)
    args = parser.parse_args()
    result = run_case(args.game)
    result["full_save_cases"] = [full_save_case(args.game, package_name) for package_name in "AB"]
    if args.oracle_dir:
        result["cpp_oracles"] = write_cpp_oracles(args.game, args.oracle_dir)
    rendered = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")


if __name__ == "__main__":
    main()
