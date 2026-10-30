import struct
import unittest

from audit_scene_coverage import background_structure, literal_arguments, raw_portals, registry


class Package:
    methods = [{"index": 0, "class_name": "LevelA", "name": "createScene"}]

    def disassemble(self, _):
        return [{"offset": 0, "mnemonic": "lookupswitch", "cases": [[0x616b0100, 20]]},
                {"offset": 20, "mnemonic": "aload_0"},
                {"offset": 21, "mnemonic": "new", "class_name": "SceneAK01"},
                {"offset": 24, "mnemonic": "goto"}]


class CoverageTests(unittest.TestCase):
    def test_factory_mapping_uses_original_case_identity(self):
        record, = registry(Package(), "LevelA")
        self.assertEqual(record["scene"], "ak01")
        self.assertEqual(record["factory_pc"], 21)

    def test_factory_mismatch_rejected(self):
        class Invalid(Package):
            def disassemble(self, index):
                code = super().disassemble(index)
                code[2]["class_name"] = "SceneAH01"
                return code
        with self.assertRaisesRegex(ValueError, "identity/class"):
            registry(Invalid(), "LevelA")

    def test_arguments_never_infer_across_loads(self):
        literal = {"opcode": 0x10, "operands": [42]}
        self.assertEqual(literal_arguments([literal, literal], 2, 2), [42, 42])
        self.assertIsNone(literal_arguments([literal, {"opcode": 0x2b}], 2, 2))

    def test_background_allocations_and_mask_ranges(self):
        payload = bytearray(b"source.jpg\0".ljust(128, b"\0"))
        payload.extend((struct.pack("<I", 4) + b"\xff\xd8\xff\xd9") * 15)
        tail = len(payload)
        payload.extend(struct.pack("<I", 1) + bytes(364))
        struct.pack_into("<f", payload, tail + 4, 1)
        for tile in range(15):
            struct.pack_into("<I", payload, tail + 24 + tile * 24, 0xffffffff)
        result = background_structure(payload, 0x616b0100, 0, None)
        self.assertEqual(result["layers"], 1)
        self.assertEqual(result["source_name"], "source.jpg")
        struct.pack_into("<I", payload, tail + 24, len(payload))
        with self.assertRaisesRegex(ValueError, "bitmask exceeds"):
            background_structure(payload, 0x616b0100, 0, None)

    def test_portal_does_not_fabricate_arrival(self):
        payload = bytearray(160)
        struct.pack_into("<2I", payload, 40, 1, 88)
        struct.pack_into("<4B", payload, 88, 0, 0, 3, 7)
        struct.pack_into("<I", payload, 88 + 56, 0x616b0900)
        portal, = raw_portals(payload)
        self.assertEqual(portal["destination"], "ak09")
        self.assertEqual(portal["marker"], 7)
        self.assertNotIn("spawn", portal)
        self.assertNotIn("arrival_resolved", portal)


if __name__ == "__main__":
    unittest.main()
