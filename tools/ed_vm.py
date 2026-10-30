"""Bounded execution of the original JPC integer/reference bytecode.

External methods in JPC have dummy bodies; unbound native calls are errors.
Class initialization is explicit until the original engine startup order is known.
"""

from dataclasses import dataclass, field


class ScriptError(ValueError):
    pass


@dataclass(eq=False)
class Object:
    class_index: int
    fields: dict = field(default_factory=dict)


@dataclass(eq=False)
class Array:
    kind: str
    values: list


def int32(value):
    if not isinstance(value, int):
        raise ScriptError("Expected a 32-bit integer")
    return (value + 0x80000000) % 0x100000000 - 0x80000000


def descriptor_types(descriptor):
    def read(index):
        start = index
        while index < len(descriptor) and descriptor[index] == "[":
            index += 1
        if index >= len(descriptor):
            raise ScriptError("Truncated method descriptor")
        if descriptor[index] == "L":
            index = descriptor.find(";", index)
            if index < 0:
                raise ScriptError("Unterminated reference descriptor")
        elif descriptor[index] not in "ZBCSIJFDV":
            raise ScriptError("Invalid method descriptor type")
        elif descriptor[index] == "V" and start != index:
            raise ScriptError("Array element cannot be void")
        return descriptor[start:index + 1], index + 1

    if not descriptor.startswith("("):
        raise ScriptError("Invalid method descriptor")
    args, index = [], 1
    while index < len(descriptor) and descriptor[index] != ")":
        kind, index = read(index)
        if kind == "V" or kind.startswith("[V"):
            raise ScriptError("Void method argument")
        args.append(kind)
    if index >= len(descriptor):
        raise ScriptError("Missing method result descriptor")
    result, end = read(index + 1)
    if end != len(descriptor) or result.startswith("[V"):
        raise ScriptError("Trailing or invalid method descriptor")
    return args, result


class Interpreter:
    def __init__(self, package, natives=None, statics=None, step_limit=100000):
        if not isinstance(step_limit, int) or step_limit <= 0:
            raise ScriptError("Invalid instruction budget")
        self.package = package
        self.natives = dict(natives or {})
        self.statics = dict(statics or {})
        self.step_limit = step_limit
        self.steps = 0
        self.trace = []
        self._active = False
        self._depth = 0
        self._code = {}
        self._initialized = set()
        self._strings = {}

    def class_index(self, name):
        matches = [c["index"] for c in self.package.classes if c["name"] == name]
        if len(matches) != 1:
            raise ScriptError(f"Expected one script class {name}")
        return matches[0]

    def method_index(self, owner, name, descriptor=None):
        matches = [m["index"] for m in self.package.methods
                   if m["class_name"] == owner and m["name"] == name
                   and (descriptor is None or m["descriptor"] == descriptor)]
        if len(matches) != 1:
            raise ScriptError(f"Expected one method {owner}.{name}{descriptor or ''}")
        return matches[0]

    def initialize(self, owner):
        index = self.class_index(owner) if isinstance(owner, str) else owner
        if index in self._initialized:
            return
        methods = [m for m in self.package.methods if m["class_index"] == index and m["name"] == "<clinit>"]
        if len(methods) > 1:
            raise ScriptError("Duplicate class initializer")
        if methods:
            self.invoke(methods[0]["index"])
        self._initialized.add(index)

    def invoke(self, method_index, receiver=None, args=()):
        outer = not self._active
        if outer:
            self.steps = 0
            self.trace.clear()
            self._active = True
        try:
            return self._call(method_index, receiver, tuple(args))
        finally:
            if outer:
                self._active = False

    def _assignable(self, actual, expected):
        seen = set()
        while actual >= 0:
            if actual in seen or actual >= len(self.package.classes):
                raise ScriptError("Invalid or cyclic script inheritance")
            if actual == expected:
                return True
            seen.add(actual)
            actual = self.package.classes[actual]["super_index"]
        return False

    def _virtual(self, index, receiver):
        declaration = self.package.methods[index]
        if not isinstance(receiver, Object) or not self._assignable(receiver.class_index, declaration["class_index"]):
            raise ScriptError("Virtual receiver is not an instance of the declared class")
        owner = receiver.class_index
        while owner >= 0:
            matches = [m for m in self.package.methods if m["class_index"] == owner
                       and m["name"] == declaration["name"] and m["descriptor"] == declaration["descriptor"]
                       and not m["access_flags"] & 8]
            if matches:
                if len(matches) != 1:
                    raise ScriptError("Ambiguous virtual method")
                return matches[0]["index"]
            owner = self.package.classes[owner]["super_index"]
        raise ScriptError("Virtual method implementation is missing")

    @staticmethod
    def _typed(value, kind):
        if kind in "ZBCSI":
            return int32(value)
        if kind.startswith(("L", "[")):
            if value != 0 and not isinstance(value, (Object, Array)):
                raise ScriptError("Invalid script reference")
            return value
        # shortcut: selected game paths use 32-bit integers/references; add wide/floating values when a verified path requires them.
        raise ScriptError(f"Unsupported execution type {kind}")

    def _call(self, index, receiver, args):
        if not isinstance(index, int) or not 0 <= index < len(self.package.methods):
            raise ScriptError("Invalid script method index")
        method = self.package.methods[index]
        label = f"{method['class_name']}.{method['name']}{method['descriptor']}"
        arg_types, result_type = descriptor_types(method["descriptor"])
        if len(args) != len(arg_types):
            raise ScriptError(f"{label}: wrong argument count")
        args = tuple(self._typed(value, kind) for value, kind in zip(args, arg_types))
        static = bool(method["access_flags"] & 8)
        if not static and (not isinstance(receiver, Object) or
                           not self._assignable(receiver.class_index, method["class_index"])):
            raise ScriptError(f"{label}: invalid instance receiver")
        if static and receiver is not None:
            raise ScriptError(f"{label}: static call has an instance receiver")
        if self._depth >= 64:
            raise ScriptError(f"{label}: script call depth exceeded")
        self._depth += 1
        pc = -1
        try:
            key = (method["class_name"], method["name"], method["descriptor"])
            if key in self.natives:
                value = self.natives[key](*args) if static else self.natives[key](receiver, *args)
                if result_type == "V":
                    if value is not None:
                        raise ScriptError("Void native returned a value")
                    return None
                return self._typed(value, result_type)
            if method["class_name"] == "ed/base/External" or not method["has_code"]:
                raise ScriptError("Unbound native method")
            if index not in self._code:
                code = self.package.disassemble(index)
                self._code[index] = (code, {item["offset"]: i for i, item in enumerate(code)})
            code, positions = self._code[index]
            local_count, stack_limit = method["max_locals"], method["max_stack"]
            if not 0 <= local_count <= 65536 or not 0 <= stack_limit <= 65536:
                raise ScriptError("Invalid frame limits")
            initial = ([] if static else [receiver]) + list(args)
            if len(initial) > local_count:
                raise ScriptError("Arguments exceed local frame")
            unset = object()
            locals_ = initial + [unset] * (local_count - len(initial))
            stack = []

            def pop():
                if not stack:
                    raise ScriptError("Operand stack underflow")
                return stack.pop()

            def local(slot):
                if not 0 <= slot < len(locals_) or locals_[slot] is unset:
                    raise ScriptError("Invalid or uninitialized local slot")
                return locals_[slot]

            def store(slot, value):
                if not 0 <= slot < len(locals_):
                    raise ScriptError("Local slot exceeds frame")
                locals_[slot] = value

            cursor = 0
            while cursor < len(code):
                item = code[cursor]
                pc, opcode, operands = item["offset"], item["opcode"], item["operands"]
                self.steps += 1
                if self.steps > self.step_limit:
                    raise ScriptError("Instruction budget exceeded")
                self.trace.append((index, pc))
                cursor += 1
                target = None
                if opcode == 0x00:
                    pass
                elif opcode == 0x01:
                    stack.append(0)
                elif 0x02 <= opcode <= 0x08:
                    stack.append(opcode - 3)
                elif opcode in (0x10, 0x11):
                    stack.append(operands[0])
                elif opcode in (0x12, 0x13):
                    constant = item["constant"]
                    if constant.get("index_raw", 0) < 0:
                        token = constant["value_raw"]
                        if token not in self._strings:
                            literal = self.package.literal_string(token)
                            self._strings[token] = Object(self.class_index("java/lang/String"), {
                                0: Array("C", literal["chars"]), 1: literal["offset"], 2: literal["count"]})
                        stack.append(self._strings[token])
                    else:
                        stack.append(constant["signed_value"])
                elif opcode in (0x15, 0x19):
                    stack.append(local(operands[0]))
                elif 0x1A <= opcode <= 0x1D:
                    stack.append(int32(local(opcode - 0x1A)))
                elif 0x2A <= opcode <= 0x2D:
                    stack.append(local(opcode - 0x2A))
                elif opcode in (0x36, 0x3A):
                    store(operands[0], pop())
                elif 0x3B <= opcode <= 0x3E:
                    store(opcode - 0x3B, int32(pop()))
                elif 0x4B <= opcode <= 0x4E:
                    store(opcode - 0x4B, pop())
                elif opcode in (0x2E, 0x32, 0x33, 0x34, 0x35, 0x4F, 0x53, 0x54, 0x55, 0x56):
                    writing = opcode >= 0x4F
                    value = pop() if writing else None
                    slot, array = int32(pop()), pop()
                    if not isinstance(array, Array) or not 0 <= slot < len(array.values):
                        raise ScriptError("Null array or array index outside its bounds")
                    kind = {0x2E: "I", 0x32: "L", 0x33: "BZ", 0x34: "C", 0x35: "S",
                            0x4F: "I", 0x53: "L", 0x54: "BZ", 0x55: "C", 0x56: "S"}[opcode]
                    if array.kind[0] not in kind:
                        raise ScriptError("Array instruction type mismatch")
                    if writing:
                        if kind == "L":
                            value = self._typed(value, "Ljava/lang/Object;")
                        else:
                            value = int32(value)
                            if array.kind in ("B", "S"):
                                bits = 8 if array.kind == "B" else 16
                                value = (value + (1 << (bits - 1))) % (1 << bits) - (1 << (bits - 1))
                            elif array.kind == "C":
                                value &= 0xFFFF
                            elif array.kind == "Z":
                                value &= 1
                        array.values[slot] = value
                    else:
                        stack.append(array.values[slot])
                elif opcode in (0x57, 0x58):
                    pop()
                    if opcode == 0x58:
                        pop()
                elif opcode in (0x59, 0x5A, 0x5B, 0x5C, 0x5F):
                    a = pop()
                    if opcode == 0x59:
                        stack.extend((a, a))
                    else:
                        b = pop()
                        if opcode == 0x5A:
                            stack.extend((a, b, a))
                        elif opcode == 0x5B:
                            c = pop()
                            stack.extend((a, c, b, a))
                        elif opcode == 0x5C:
                            stack.extend((b, a, b, a))
                        else:
                            stack.extend((a, b))
                elif opcode in (0x60, 0x64, 0x68, 0x6C, 0x70, 0x78, 0x7A, 0x7C, 0x7E, 0x80, 0x82):
                    b, a = int32(pop()), int32(pop())
                    if opcode in (0x6C, 0x70):
                        if not b:
                            raise ScriptError("Integer division by zero")
                        quotient = (abs(a) // abs(b)) * (-1 if (a < 0) != (b < 0) else 1)
                        value = quotient if opcode == 0x6C else a - quotient * b
                    else:
                        value = {0x60: lambda: a + b, 0x64: lambda: a - b, 0x68: lambda: a * b,
                                 0x78: lambda: a << (b & 31), 0x7A: lambda: a >> (b & 31),
                                 0x7C: lambda: (a & 0xFFFFFFFF) >> (b & 31), 0x7E: lambda: a & b,
                                 0x80: lambda: a | b, 0x82: lambda: a ^ b}[opcode]()
                    stack.append(int32(value))
                elif opcode == 0x74:
                    stack.append(int32(-int32(pop())))
                elif opcode == 0x84:
                    store(operands[0], int32(int32(local(operands[0])) + operands[1]))
                elif opcode in (0x91, 0x92, 0x93):
                    value = int32(pop())
                    bits = 8 if opcode == 0x91 else 16
                    stack.append(value & 0xFFFF if opcode == 0x92 else
                                 (value + (1 << (bits - 1))) % (1 << bits) - (1 << (bits - 1)))
                elif 0x99 <= opcode <= 0xA6:
                    if opcode <= 0x9E:
                        a, b, condition = int32(pop()), 0, opcode - 0x99
                    else:
                        b, a, condition = pop(), pop(), opcode - 0x9F
                        if opcode < 0xA5:
                            a, b = int32(a), int32(b)
                        else:
                            condition = opcode - 0xA5
                            a, b = (a is b or a == b == 0), True
                    if (a == b, a != b, a < b, a >= b, a > b, a <= b)[condition]:
                        target = item["target"]
                elif opcode in (0xA7, 0xC8):
                    target = item["target"]
                elif opcode in (0xAA, 0xAB):
                    value = int32(pop())
                    target = dict(item["cases"]).get(value, item["default"])
                elif opcode in (0xAC, 0xB0, 0xB1):
                    if opcode == 0xB1:
                        if result_type != "V":
                            raise ScriptError("Void return in a value method")
                        return None
                    value = pop()
                    if result_type == "V":
                        raise ScriptError("Value return in a void method")
                    return self._typed(value, result_type)
                elif 0xB2 <= opcode <= 0xB5:
                    field_index = item["field_index"]
                    value = pop() if opcode in (0xB3, 0xB5) else None
                    if opcode in (0xB2, 0xB3):
                        fields = self.statics
                    else:
                        obj = pop()
                        if not isinstance(obj, Object):
                            raise ScriptError("Null or invalid object field receiver")
                        fields = obj.fields
                    if opcode in (0xB3, 0xB5):
                        fields[field_index] = value
                    else:
                        stack.append(fields.get(field_index, 0))
                elif opcode in (0xB6, 0xB7, 0xB8, 0xB9):
                    called = item["method"]
                    types, result = descriptor_types(called["descriptor"])
                    arguments = tuple(reversed([pop() for _ in types]))
                    instance = None if opcode == 0xB8 else pop()
                    called_index = called["index"]
                    if opcode in (0xB6, 0xB9):
                        called_index = self._virtual(called_index, instance)
                    returned = self._call(called_index, instance, arguments)
                    if result != "V":
                        stack.append(returned)
                elif opcode == 0xBB:
                    stack.append(Object(item["constant"]["value_raw"]))
                elif opcode in (0xBC, 0xBD):
                    count = int32(pop())
                    if not 0 <= count <= 1000000:
                        raise ScriptError("Invalid or excessive array length")
                    kind = "L" if opcode == 0xBD else {4: "Z", 5: "C", 8: "B", 9: "S", 10: "I"}.get(operands[0])
                    if kind is None:
                        raise ScriptError("Unsupported primitive array type")
                    stack.append(Array(kind, [0] * count))
                elif opcode == 0xBE:
                    array = pop()
                    if not isinstance(array, Array):
                        raise ScriptError("Null or invalid array reference")
                    stack.append(len(array.values))
                elif opcode in (0xC0, 0xC1):
                    obj = pop()
                    expected = item["constant"]["value_raw"]
                    if not 0 <= expected < len(self.package.classes):
                        raise ScriptError("Invalid cast class")
                    matches = (self.package.classes[expected]["name"] == "java/lang/Object" if isinstance(obj, Array)
                               else isinstance(obj, Object) and self._assignable(obj.class_index, expected))
                    if opcode == 0xC1:
                        stack.append(int(matches))
                    elif obj == 0 or matches:
                        stack.append(obj)
                    else:
                        raise ScriptError("Script class cast failed")
                elif opcode == 0xC4:
                    wide = item["wide_opcode"]
                    if wide in (0x15, 0x19):
                        stack.append(local(operands[0]))
                    elif wide in (0x36, 0x3A):
                        store(operands[0], pop())
                    elif wide == 0x84:
                        store(operands[0], int32(int32(local(operands[0])) + operands[1]))
                    else:
                        raise ScriptError("Unsupported wide instruction")
                elif opcode in (0xC6, 0xC7):
                    value = pop()
                    if (value == 0) == (opcode == 0xC6):
                        target = item["target"]
                else:
                    raise ScriptError(f"Unsupported opcode {item['mnemonic']} (0x{opcode:02x})")
                if len(stack) > stack_limit:
                    raise ScriptError("Operand stack exceeds declared frame")
                if target is not None:
                    if target not in positions:
                        raise ScriptError("Invalid branch destination")
                    cursor = positions[target]
            raise ScriptError("Method fell through without returning")
        except ScriptError as error:
            raise ScriptError(f"{label} @ {pc}: {error}") from error
        except Exception as error:
            raise ScriptError(f"{label} @ {pc}: invalid script operation: {error}") from error
        finally:
            self._depth -= 1
