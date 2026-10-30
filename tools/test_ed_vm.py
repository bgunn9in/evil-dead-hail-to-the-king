import struct
import unittest

from ed_scripts import decode_bytecode
from ed_vm import Array, Interpreter, Object, ScriptError, descriptor_types


class Package:
    """Small synthetic methods; original JPC is exercised by the case runners."""
    def __init__(self):
        self.classes = [{"index": i, "name": name, "super_index": parent}
                        for i, (name, parent) in enumerate((("Base", -1), ("Derived", 0),
                                                           ("ed/base/External", -1)))]
        self.methods = []
        self.code = []
        self.constants = {}

    def add(self, code, descriptor="()I", owner=0, name="run", static=True, locals_=8, stack=16):
        index = len(self.methods)
        self.methods.append({"index": index, "class_index": owner, "class_name": self.classes[owner]["name"],
                             "name": name, "descriptor": descriptor, "access_flags": 8 if static else 1,
                             "has_code": True, "max_locals": locals_, "max_stack": stack})
        self.code.append(bytes.fromhex(code))
        return index

    def disassemble(self, index):
        code = decode_bytecode(self.code[index])
        for item in code:
            op = item["opcode"]
            if op in (0x12, 0x13):
                item["constant"] = {"signed_value": self.constants[item["operands"][0]]}
            elif 0xB2 <= op <= 0xB5:
                item["field_index"] = item["operands"][0]
            elif 0xB6 <= op <= 0xB9:
                item["method"] = self.methods[item["operands"][0]]
            elif op in (0xBB, 0xBD, 0xC0, 0xC1):
                item["constant"] = {"value_raw": item["operands"][0]}
        return code


class VmTests(unittest.TestCase):
    def test_arrays_cast_to_object_without_changing_element_kind(self):
        package = Package()
        package.classes[0]["name"] = "java/lang/Object"
        cast = package.add("2a c00000 b0", descriptor="(Ljava/lang/Object;)Ljava/lang/Object;")
        is_object = package.add("2a c10000 ac", descriptor="(Ljava/lang/Object;)I")
        wrong = package.add("2a c00001 b0", descriptor="(Ljava/lang/Object;)Ljava/lang/Object;")
        vm = Interpreter(package)
        for kind in "BCSIZL":
            array = Array(kind, [0])
            self.assertIs(vm.invoke(cast, args=(array,)), array)
            self.assertEqual(vm.invoke(is_object, args=(array,)), 1)
            self.assertEqual(array.kind, kind)
            with self.assertRaises(ScriptError):
                vm.invoke(wrong, args=(array,))
        self.assertEqual(vm.invoke(is_object, args=(0,)), 0)

    def run_code(self, code, args=(), descriptor="()I", constants=None, **limits):
        package = Package()
        package.constants = constants or {}
        index = package.add(code, descriptor, **limits)
        vm = Interpreter(package)
        return vm.invoke(index, args=args)

    def test_integer_overflow_division_remainder_and_shift(self):
        self.assertEqual(self.run_code("1201 04 60 ac", constants={1: 0x7FFFFFFF}), -0x80000000)
        self.assertEqual(self.run_code("10f9 06 6c ac"), -2)
        self.assertEqual(self.run_code("10f9 06 70 ac"), -1)
        self.assertEqual(self.run_code("1201 02 6c ac", constants={1: -0x80000000}), -0x80000000)
        self.assertEqual(self.run_code("02 04 7c ac"), 0x7FFFFFFF)
        self.assertEqual(self.run_code("04 1020 78 ac"), 1)
        self.assertEqual(self.run_code("11ffff 92 ac"), 65535)

    def test_locals_conditional_branches_and_wide_increment(self):
        self.assertEqual(self.run_code("1a 990005 04ac 03ac", args=(0,), descriptor="(I)I"), 0)
        self.assertEqual(self.run_code("1a 990005 04ac 03ac", args=(5,), descriptor="(I)I"), 1)
        self.assertEqual(self.run_code("04 3b c4840000fffe 1a ac"), -1)

    def test_switch_uses_signed_key_and_default(self):
        package = Package()
        index = package.add("00")
        package.code[index] = b"\x1a\xab\0\0" + struct.pack(">4i", 21, 1, -7, 19) + b"\x04\xac\x03\xac"
        vm = Interpreter(package)
        package.methods[index]["descriptor"] = "(I)I"
        self.assertEqual(vm.invoke(index, args=(-7,)), 1)
        self.assertEqual(vm.invoke(index, args=(1,)), 0)

    def test_virtual_dispatch_fields_and_argument_order(self):
        package = Package()
        base = package.add("2a b40005 ac", "()I", name="get", static=False)
        package.add("2a b40005 05 68 ac", "()I", owner=1, name="get", static=False)
        run = package.add("2a 10 09 b50005 2a b60000 ac", "()I", name="call", static=False)
        obj = Object(1)
        vm = Interpreter(package)
        self.assertEqual(vm.invoke(run, obj), 18)
        self.assertEqual(obj.fields[5], 9)
        with self.assertRaisesRegex(ScriptError, "invalid instance receiver"):
            vm.invoke(base, Object(2))
        native = package.add("03ac", "(II)I", owner=2, name="difference")
        caller = package.add(f"1009 06 b8{native:04x} ac", name="order")
        vm.natives[("ed/base/External", "difference", "(II)I")] = lambda a, b: a - b
        self.assertEqual(vm.invoke(caller), 6)

    def test_explicit_class_initialization_is_once_and_writes_statics(self):
        package = Package()
        package.add("b20003 04 60 b30003 b1", "()V", name="<clinit>")
        vm = Interpreter(package)
        vm.initialize("Base")
        vm.initialize("Base")
        self.assertEqual(vm.statics[3], 1)

    def test_arrays_aliasing_types_and_bounds(self):
        self.assertEqual(self.run_code("06 bc0a 59 04 1009 4f 04 2e ac"), 9)
        self.assertEqual(self.run_code("04 bc05 59 03 02 55 03 34 ac"), 65535)
        package = Package()
        index = package.add("2a 03 2e ac", "([I)I")
        vm = Interpreter(package)
        array = Array("I", [42])
        self.assertEqual(vm.invoke(index, args=(array,)), 42)
        with self.assertRaisesRegex(ScriptError, "outside its bounds"):
            vm.invoke(index, args=(Array("I", []),))
        with self.assertRaisesRegex(ScriptError, "type mismatch"):
            vm.invoke(index, args=(Array("C", [42]),))

    def test_new_constructor_and_reference_identity(self):
        package = Package()
        package.add("2a 1b b50005 b1", "(I)V", name="<init>", static=False)
        run = package.add("bb0001 59 1007 b70000 59 b40005 ac", name="allocate")
        vm = Interpreter(package)
        self.assertEqual(vm.invoke(run), 7)
        same = package.add("2a 2b a60005 04ac 03ac", "(LBase;LBase;)I", name="same")
        obj = Object(0)
        self.assertEqual(vm.invoke(same, args=(obj, obj)), 1)
        self.assertEqual(vm.invoke(same, args=(obj, Object(0))), 0)

    def test_dummy_external_body_cannot_silently_run(self):
        package = Package()
        index = package.add("b1", "()V", owner=2, name="nativeEffect")
        with self.assertRaisesRegex(ScriptError, "nativeEffect.*Unbound native"):
            Interpreter(package).invoke(index)
        events = []
        vm = Interpreter(package, {("ed/base/External", "nativeEffect", "()V"): lambda: events.append("called")})
        vm.invoke(index)
        self.assertEqual(events, ["called"])

    def test_unknown_operations_and_budgets_have_method_and_pc(self):
        package = Package()
        loop = package.add("a70000", name="loop")
        vm = Interpreter(package, step_limit=9)
        with self.assertRaisesRegex(ScriptError, r"Base.loop\(\)I @ 0: Instruction budget"):
            vm.invoke(loop)
        self.assertEqual(len(vm.trace), 9)
        unsupported = package.add("0bac", name="floatPath")
        with self.assertRaisesRegex(ScriptError, "floatPath.*Unsupported opcode fconst_0"):
            vm.invoke(unsupported)
        recursive = package.add(f"b8{len(package.methods):04x} ac", name="recursive")
        with self.assertRaisesRegex(ScriptError, "call depth exceeded"):
            Interpreter(package).invoke(recursive)

    def test_bad_frames_fail_instead_of_fabricating_values(self):
        for code, limits, expected in (("ac", {}, "underflow"), ("1bac", {}, "uninitialized"),
                                       ("0405ac", {"stack": 1}, "exceeds"), ("00", {}, "fell through"),
                                       ("04036cac", {}, "division by zero")):
            with self.subTest(code=code), self.assertRaisesRegex(ScriptError, expected):
                self.run_code(code, **limits)

    def test_descriptor_parsing_and_rejection(self):
        self.assertEqual(descriptor_types("(I[LBase;[[I)Z"), (["I", "[LBase;", "[[I"], "Z"))
        for value in ("I)V", "(V)V", "(I", "()", "(Lbad)V", "()Vjunk", "([V)V", "([[V)V"):
            with self.subTest(value=value), self.assertRaises(ScriptError):
                descriptor_types(value)


if __name__ == "__main__":
    unittest.main()
