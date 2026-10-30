from pathlib import Path
import unittest

from audit_levelb_coverage import literal_arguments, native_actor, script_capacities
from ed_actions import _va_reader
from ed_scripts import ScriptPackage


class PoolPackage:
    methods = [{"index": 0, "class_name": "ActorDB", "name": "initialize"}]
    def disassemble(self, _):
        code = []
        for native, count in ((4, 4), (19, 1)):
            code.extend([{"offset": len(code), "mnemonic": "getstatic", "field_index": 54},
                         {"opcode": 0x10, "operands": [native], "mnemonic": "bipush"},
                         {"mnemonic": "iload_0"}, {"mnemonic": "iastore"},
                         {"mnemonic": "iinc", "operands": [0, count]}])
        return code


class LevelBAuditTests(unittest.TestCase):
    def test_script_capacity_is_original_adjacent_allocation(self):
        result = script_capacities(PoolPackage())
        self.assertEqual((result[4]["base"], result[4]["capacity"]), (0, 4))
        self.assertEqual((result[19]["base"], result[19]["capacity"]), (4, 1))
        class Invalid(PoolPackage):
            def disassemble(self, index):
                code = super().disassemble(index)
                code[3] = {"mnemonic": "nop"}
                return code
        with self.assertRaisesRegex(ValueError, "allocation"):
            script_capacities(Invalid())

    @unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
    def test_original_levelB_namespace_aliases_and_native_capacities(self):
        game = Path(__file__).resolve().parents[1] / "game"
        if not (game / "EvilDead.exe").is_file():
            self.skipTest("Original user game not present")
        package = ScriptPackage((game / "data/script/pc/levelB.jpc").read_bytes())
        pools, read = script_capacities(package), _va_reader((game / "EvilDead.exe").read_bytes())
        for native, count in ((19, 1), (23, 4), (24, 4), (26, 1), (27, 1), (28, 4), (29, 1), (31, 4), (33, 1), (38, 1), (39, 1)):
            self.assertEqual(pools[native]["capacity"], count)
            self.assertEqual(native_actor(read, native)["capacity"], count)
        create = next(m for m in package.methods if m["class_name"] == "ActorDB" and m["name"] == "create")
        code = package.disassemble(create["index"])
        switch = next(i for i in code if i["mnemonic"] == "lookupswitch")
        offsets = {i["offset"]: i for i in code}
        classes = {key: offsets[target]["class_name"] for key, target in switch["cases"]}
        self.assertEqual(classes[19], "BadAsh")
        self.assertEqual(classes[31], "NetherPet")
        self.assertEqual(classes[5], "java/lang/Object")
        create_type = next(m for m in package.methods if m["class_name"] == "ActorDB" and m["name"] == "createType")
        code = package.disassemble(create_type["index"])
        switch = next(i for i in code if i["mnemonic"] == "lookupswitch")
        targets = dict(switch["cases"])
        variants = []
        for prototype in (31, 36, 37):
            call = next(n for n, ins in enumerate(code) if ins["offset"] >= targets[prototype]
                        and ins.get("method", {}).get("class_name") == "ActorDB")
            args = literal_arguments(code, call, 4)
            self.assertEqual(args[0], 31)
            variants.append(tuple(args[1:]))
        self.assertEqual(len(set(variants)), 3)
        with self.assertRaisesRegex(ValueError, "descriptor table"):
            native_actor(read, 40)


if __name__ == "__main__":
    unittest.main()
