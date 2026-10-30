import math
import struct
import unittest

from ed_scripts import ScriptPackage, decode_bytecode, extract_portal_policy, extract_portal_timing, source_facing_allows


def fixture(activation=None, allow=None, padding=0):
    """Synthetic JPC package; no original game bytecode is embedded in tests."""
    strings = bytearray()

    def string(value):
        offset = len(strings)
        strings.extend(value.encode() + b"\0")
        return offset

    class_names = [string(name) for name in ("SceneTest", "ed/base/EDActor", "ed/base/EDScene", "GenericScene")]
    definitions = [("activateScene", "(Z)V", 0), ("allowUse", "(LAsh;I)I", 0),
                   ("inTrigger", "(I)Z", 1), ("facingMarker", "(II)Z", 1),
                   ("portalDisable", "(I)V", 2), ("portalEnableSoundPlay", "(II)V", 3),
                   ("portalEnable", "(I)V", 2)]
    names = [(string(name), string(descriptor), owner) for name, descriptor, owner in definitions]
    if activation is None:
        activation = bytes.fromhex("05 b8 0004 b1")
    if allow is None:
        # inTrigger(7), facingMarker(9,40), action=116, helper(2,sound).
        allow = bytes.fromhex("2b 1007 b6 0002 99 001a 2b 1009 1028 b6 0003 99 000f 1074 3d 05 1208 b8 0005 a7 0003 1c ac")
    code = bytes(padding) + activation + allow
    method_data = bytearray()
    for index, (name, descriptor, owner) in enumerate(names):
        native = index >= 2
        offset = 0xFFFFFFFF if native else padding + (len(activation) if index else 0)
        length = 0xCDCD if native else len(allow if index else activation)
        method_data.extend(struct.pack("<10hI", name, descriptor, 0x100 if native else 1, 0,
                                       -12851, owner, 0, 3, 3, length if length < 32768 else length - 65536, offset))
    class_data = b"".join(struct.pack("<10h", name, 0, 0, 0, 0, 0, len(names), 0, -1, index)
                          for index, name in enumerate(class_names))
    values = [0, 0, 2, 3, 4, 5, 6, 0, 0x0E112233]
    header = struct.pack("<14I", 0, len(names), len(class_names), len(code), len(strings), len(values), len(values), *([0] * 7))
    return header + code + method_data + class_data + strings + struct.pack("<9h", *range(9)) + struct.pack("<9I", *values)


class ScriptTests(unittest.TestCase):
    def test_package_full_32_bit_code_offset_and_resolved_invocation(self):
        package = ScriptPackage(fixture(padding=70000))
        self.assertEqual(package.methods[0]["code_offset"], 70000)
        self.assertFalse(package.methods[2]["has_code"])
        self.assertEqual(package.disassemble(2), [])
        self.assertEqual(package.disassemble(0)[1]["method"]["name"], "portalDisable")
        self.assertEqual(package.disassemble(1)[12]["constant"]["value_raw"], 0x0E112233)

    def test_source_gate_and_unconditional_initial_state(self):
        package = ScriptPackage(fixture())
        policy = extract_portal_policy(package, "SceneTest", [2])
        self.assertEqual(policy["initially_disabled"], [2])
        gate = policy["gates"][0]
        self.assertEqual([gate[key] for key in ("portal_index", "trigger_index", "marker_index",
                                              "angle_argument_degrees", "action_assigned")], [2, 7, 9, 40, 116])
        conditional = bytes.fromhex("03 99 0007 05 b8 0004 b1")
        with self.assertRaisesRegex(ValueError, "unconditional"):
            extract_portal_policy(ScriptPackage(fixture(activation=conditional)), "SceneTest", [2])
        with self.assertRaisesRegex(ValueError, "Missing"):
            extract_portal_policy(package, "SceneTest", [3])

    def test_reject_changed_or_bypassed_source_predicate(self):
        package = ScriptPackage(fixture())
        instructions = package.disassemble(1)
        # A changed predicate must fail, rather than silently inventing its meaning.
        instructions[7]["method"] = dict(instructions[7]["method"], name="differentPredicate")
        original = package.disassemble
        package.disassemble = lambda index: instructions if index == 1 else original(index)
        with self.assertRaisesRegex(ValueError, "Unsupported"):
            extract_portal_policy(package, "SceneTest", [2])
        instructions[7]["method"]["name"] = "facingMarker"
        instructions[14]["target"] = instructions[4]["offset"]
        with self.assertRaisesRegex(ValueError, "bypasses"):
            extract_portal_policy(package, "SceneTest", [2])

    def test_bounded_tables_and_constant_references(self):
        with self.assertRaises(ValueError):
            ScriptPackage(fixture()[:-1])
        package = ScriptPackage(fixture())
        for args in ((-1, 0), (0, -1), (0, 9)):
            with self.assertRaises(ValueError):
                package.constant(*args)
        changed = bytearray(fixture())
        struct.pack_into("<h", changed, package.index_offset + 4, 99)
        with self.assertRaises(ValueError):
            ScriptPackage(changed).disassemble(1)

    def test_literal_heap_checks_string_and_byte_array_ranges(self):
        heap = bytearray(48)
        struct.pack_into("<3I", heap, 0, 0, 1, 4)
        heap[12:16] = b"abc\0"
        struct.pack_into("<6I", heap, 24, 1, 4, 3, 12, 1, 2)
        data = fixture() + struct.pack("<I", len(heap)) + heap
        self.assertEqual(ScriptPackage(data).literal_string(36)["text"], "bc")
        for relative, value in ((24, 0), (36, 999), (40, 5), (44, 99)):
            changed = bytearray(data)
            struct.pack_into("<I", changed, len(data) - len(heap) + relative, value)
            with self.subTest(relative=relative), self.assertRaises(ValueError):
                ScriptPackage(changed).literal_string(36)
        for token in (-1, 0, 999):
            with self.assertRaises(ValueError):
                ScriptPackage(data).literal_string(token)

    def test_instruction_boundaries_switches_and_signed_operands(self):
        switch = b"\xab\0\0\0" + struct.pack(">4i", 20, 1, 140, 20) + b"\xb1"
        self.assertEqual(decode_bytecode(switch)[0]["cases"], [[140, 20]])
        self.assertEqual(decode_bytecode(bytes.fromhex("10ff 8402ff c4840003fffe b1"))[2]["operands"], [3, -2])
        for bad in (bytes.fromhex("a70001b1"), b"\x10", b"\xc4\x00", b"\xcb",
                    b"\xaa\0\0\0" + struct.pack(">3i", 0, 1, 0)):
            with self.subTest(bad=bad.hex()), self.assertRaises(ValueError):
                decode_bytecode(bad)

    def test_original_facing_integer_conversion_and_strict_boundary(self):
        for angle in (40, 45, -40):
            self.assertTrue(source_facing_allows(0.99, angle))
            self.assertTrue(source_facing_allows(-0.99, angle))
            self.assertFalse(source_facing_allows(1.0, angle))
            self.assertFalse(source_facing_allows(-1.0, angle))
            self.assertTrue(source_facing_allows(2 * math.pi + 0.5, angle))
        self.assertFalse(source_facing_allows(0, 0))
        self.assertTrue(source_facing_allows(math.pi, 180))
        with self.assertRaises(ValueError):
            source_facing_allows(float("nan"), 40)

    def test_sound_timing_is_read_from_source_literals_and_field_links(self):
        helper = bytes.fromhex("1a b3000a 04 b3000b 1b b80001 b3000c b2000c b80002 b3000d 100c b3000e 04 b3000f 1013 b30010 b1")
        frame = bytes.fromhex("b2000b 990030 b2000e 02 60 b3000e b2000d b80003 9a0009 b2000e 9e000b b2000e 10f9 a30011 03 b3000e b2000a b80004 03 b3000b b1")
        main = bytes.fromhex("b2000f 990015 b20010 02 60 b30010 b20010 9d0007 03 b3000f b1")

        class TimingPackage:
            methods = [{"index": i, "class_name": owner, "name": name} for i, (owner, name) in enumerate(
                [("GenericScene", "portalEnableSoundPlay"), ("GenericScene", "soundTrackProcessFrame"), ("Main", "processFrame")])]

            def disassemble(self, index):
                instructions = decode_bytecode((helper, frame, main)[index])
                for item in instructions:
                    if item["opcode"] in (0xB2, 0xB3):
                        item["constant"] = {"value_raw": item["operands"][0]}
                    if item["opcode"] == 0xB8:
                        owner, name = {1: ("ed/base/EDBase", "getSNDID"), 2: ("ed/base/EDBase", "soundPlay"),
                                       3: ("ed/base/EDBase", "isVoicePlaying"), 4: ("ed/base/EDScene", "portalEnable")}[item["operands"][0]]
                        item["method"] = {"class_name": owner, "name": name}
                return instructions

        result = extract_portal_timing(TimingPackage())
        self.assertEqual([result[key] for key in ("delay_ticks", "forced_enable_countdown", "latest_enable_ticks", "input_guard_ticks")], [12, -7, 19, 19])
        self.assertEqual(result["fields"]["pending_portal"], 10)


if __name__ == "__main__":
    unittest.main()
