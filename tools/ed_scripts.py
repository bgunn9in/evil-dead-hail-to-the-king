"""Read the original JPC package tables and disassemble selected JVM methods."""

import math
import struct


_NAMES = """nop aconst_null iconst_m1 iconst_0 iconst_1 iconst_2 iconst_3 iconst_4 iconst_5
lconst_0 lconst_1 fconst_0 fconst_1 fconst_2 dconst_0 dconst_1 bipush sipush ldc ldc_w ldc2_w
iload lload fload dload aload iload_0 iload_1 iload_2 iload_3 lload_0 lload_1 lload_2 lload_3
fload_0 fload_1 fload_2 fload_3 dload_0 dload_1 dload_2 dload_3 aload_0 aload_1 aload_2 aload_3
iaload laload faload daload aaload baload caload saload istore lstore fstore dstore astore
istore_0 istore_1 istore_2 istore_3 lstore_0 lstore_1 lstore_2 lstore_3 fstore_0 fstore_1 fstore_2 fstore_3
dstore_0 dstore_1 dstore_2 dstore_3 astore_0 astore_1 astore_2 astore_3 iastore lastore fastore dastore
aastore bastore castore sastore pop pop2 dup dup_x1 dup_x2 dup2 dup2_x1 dup2_x2 swap
iadd ladd fadd dadd isub lsub fsub dsub imul lmul fmul dmul idiv ldiv fdiv ddiv
irem lrem frem drem ineg lneg fneg dneg ishl lshl ishr lshr iushr lushr iand land ior lor ixor lxor
iinc i2l i2f i2d l2i l2f l2d f2i f2l f2d d2i d2l d2f i2b i2c i2s lcmp fcmpl fcmpg dcmpl dcmpg
ifeq ifne iflt ifge ifgt ifle if_icmpeq if_icmpne if_icmplt if_icmpge if_icmpgt if_icmple if_acmpeq if_acmpne
goto jsr ret tableswitch lookupswitch ireturn lreturn freturn dreturn areturn return
getstatic putstatic getfield putfield invokevirtual invokespecial invokestatic invokeinterface invokedynamic
new newarray anewarray arraylength athrow checkcast instanceof monitorenter monitorexit wide multianewarray
ifnull ifnonnull goto_w jsr_w breakpoint""".split()


def decode_bytecode(code: bytes) -> list[dict]:
    """Decode instruction boundaries, signed immediates and branch destinations."""
    instructions = []
    offset = 0
    while offset < len(code):
        start, opcode = offset, code[offset]
        offset += 1
        if opcode >= len(_NAMES) or opcode == 0xCA:
            raise ValueError(f"Unsupported JVM opcode 0x{opcode:02x} at {start}")
        record = {"offset": start, "opcode": opcode, "mnemonic": _NAMES[opcode], "operands": []}

        def take(fmt):
            nonlocal offset
            length = struct.calcsize(fmt)
            if offset + length > len(code):
                raise ValueError(f"Truncated JVM instruction at {start}")
            value = struct.unpack_from(fmt, code, offset)
            offset += length
            return value

        if opcode in (0xAA, 0xAB):
            offset += -offset % 4
            default = take(">i")[0]
            record["default"] = start + default
            if opcode == 0xAA:
                low, high = take(">2i")
                count = high - low + 1
                if count < 1 or count > (len(code) - offset) // 4:
                    raise ValueError("Invalid JVM tableswitch range")
                pairs = [(key, take(">i")[0]) for key in range(low, high + 1)]
            else:
                count = take(">i")[0]
                if count < 0 or count > (len(code) - offset) // 8:
                    raise ValueError("Invalid JVM lookupswitch count")
                pairs = [take(">2i") for _ in range(count)]
                if any(pairs[index][0] >= pairs[index + 1][0] for index in range(len(pairs) - 1)):
                    raise ValueError("Unsorted JVM lookupswitch keys")
            record["cases"] = [[key, start + target] for key, target in pairs]
        elif opcode == 0xC4:
            widened = take(">B")[0]
            if widened not in (*range(0x15, 0x1A), *range(0x36, 0x3B), 0x84, 0xA9):
                raise ValueError("Invalid JVM wide instruction")
            record["wide_opcode"] = widened
            record["operands"] = list(take(">Hh" if widened == 0x84 else ">H"))
        elif opcode == 0x10:
            record["operands"] = list(take(">b"))
        elif opcode == 0x11:
            record["operands"] = list(take(">h"))
        elif opcode in (0x12, 0xBC, 0xA9, *range(0x15, 0x1A), *range(0x36, 0x3B)):
            record["operands"] = list(take(">B"))
        elif opcode == 0x84:
            record["operands"] = list(take(">Bb"))
        elif opcode in (*range(0x99, 0xA9), 0xC6, 0xC7):
            record["operands"] = list(take(">h"))
            record["target"] = start + record["operands"][0]
        elif opcode in (0xC8, 0xC9):
            record["operands"] = list(take(">i"))
            record["target"] = start + record["operands"][0]
        elif opcode in (0xB9, 0xBA):
            record["operands"] = list(take(">HBB"))
        elif opcode == 0xC5:
            record["operands"] = list(take(">HB"))
        elif opcode in (0x13, 0x14, *range(0xB2, 0xB9), 0xBB, 0xBD, 0xC0, 0xC1):
            record["operands"] = list(take(">H"))
        record["bytes"] = code[start:offset].hex()
        instructions.append(record)
    boundaries = {instruction["offset"] for instruction in instructions}
    for instruction in instructions:
        targets = ([instruction["target"]] if "target" in instruction else [])
        if "default" in instruction:
            targets += [instruction["default"], *(pair[1] for pair in instruction["cases"])]
        if any(target not in boundaries for target in targets):
            raise ValueError("JVM branch does not target an instruction boundary")
    return instructions


class ScriptPackage:
    def __init__(self, data: bytes):
        if len(data) < 56:
            raise ValueError("Truncated JPC header")
        self.data = data
        fields, methods, classes, code_size, string_size, values, indices = struct.unpack_from("<7I", data)
        method_offset = 56 + code_size
        field_offset = method_offset + methods * 24
        class_offset = field_offset + fields * 8
        self.string_offset = class_offset + classes * 20
        self.string_size = string_size
        self.index_offset = self.string_offset + string_size
        self.index_count = indices
        self.value_offset = self.index_offset + indices * 2
        self.value_count = values
        if self.value_offset + values * 4 > len(data):
            raise ValueError("JPC sections exceed package bounds")
        self.code_size = code_size
        self.classes = []
        for index in range(classes):
            record = struct.unpack_from("<10h", data, class_offset + index * 20)
            item = {"index": index, "name": self.string(record[0]), "constant_pool_base": record[1],
                    "first_method": record[4], "method_count": record[6], "super_index": record[8],
                    "raw": list(record)}
            if (not 0 <= item["constant_pool_base"] <= indices or not 0 <= item["first_method"] <= methods
                    or not 0 <= item["method_count"] <= methods - item["first_method"]
                    or not -1 <= item["super_index"] < classes):
                raise ValueError("Invalid JPC class table reference")
            self.classes.append(item)
        self.methods = []
        for index in range(methods):
            position = method_offset + index * 24
            record = struct.unpack_from("<10hI", data, position)
            class_index = record[5]
            code_length = struct.unpack_from("<H", data, position + 18)[0]
            has_code = record[10] != 0xFFFFFFFF
            if (not 0 <= class_index < classes or (not has_code and not record[2] & 0x500)
                    or (has_code and (record[10] > code_size or code_length > code_size - record[10]))):
                raise ValueError("Invalid JPC method code range")
            self.methods.append({"index": index, "name": self.string(record[0]),
                                 "descriptor": self.string(record[1]), "class_index": class_index,
                                 "class_name": self.classes[class_index]["name"],
                                 "access_flags": record[2], "max_stack": record[7], "max_locals": record[8],
                                 "has_code": has_code,
                                 "code_length": code_length if has_code else 0, "code_offset": record[10]})
        self.fields_raw = [list(struct.unpack_from("<4h", data, field_offset + index * 8)) for index in range(fields)]

    def literal_string(self, token: int) -> dict:
        """Read the serialized String/8-bit char[] referenced by a negative ldc index."""
        size_offset = self.value_offset + self.value_count * 4
        if size_offset + 4 > len(self.data):
            raise ValueError("JPC literal heap is missing")
        size = struct.unpack_from("<I", self.data, size_offset)[0]
        base = size_offset + 4
        if size > len(self.data) - base:
            raise ValueError("JPC literal heap exceeds package bounds")

        def header(reference, payload_size):
            if not isinstance(reference, int) or reference < 12 or reference > size - payload_size:
                raise ValueError("JPC literal reference exceeds heap bounds")
            return struct.unpack_from("<3I", self.data, base + reference - 12)

        if header(token, 12) != (1, 4, 3):
            raise ValueError("JPC literal is not a serialized String")
        chars, offset, count = struct.unpack_from("<3I", self.data, base + token)
        kind, width, length = header(chars, 0)
        if (kind, width) != (0, 1) or length > size - chars or offset > length or count > length - offset:
            raise ValueError("Invalid JPC literal char array or string range")
        values = self.data[base + chars:base + chars + length]
        return {"token": token, "chars": list(values), "offset": offset, "count": count,
                "text": values[offset:offset + count].decode("latin1")}

    def string(self, offset: int) -> str:
        if not 0 <= offset < self.string_size:
            raise ValueError("JPC string reference exceeds its table")
        start = self.string_offset + offset
        end = self.data.find(b"\0", start, self.string_offset + self.string_size)
        if end < 0:
            raise ValueError("Unterminated JPC string")
        return self.data[start:end].decode("utf-8", errors="replace")

    def constant(self, class_index: int, operand: int) -> dict:
        if not 0 <= class_index < len(self.classes) or operand < 0:
            raise ValueError("Invalid JPC constant reference")
        index = self.classes[class_index]["constant_pool_base"] + operand
        if index >= self.index_count:
            raise ValueError("JPC constant index exceeds its table")
        value_index = struct.unpack_from("<h", self.data, self.index_offset + index * 2)[0]
        if abs(value_index) >= self.value_count:
            raise ValueError("JPC constant value exceeds its table")
        value = struct.unpack_from("<I", self.data, self.value_offset + abs(value_index) * 4)[0]
        signed = struct.unpack("<i", struct.pack("<I", value))[0]
        return {"operand": operand, "index_raw": value_index, "value_raw": value, "signed_value": signed}

    def disassemble(self, method_index: int) -> list[dict]:
        if not 0 <= method_index < len(self.methods):
            raise ValueError("Invalid JPC method index")
        method = self.methods[method_index]
        if not method["has_code"]:
            return []
        start = 56 + method["code_offset"]
        instructions = decode_bytecode(self.data[start:start + method["code_length"]])
        for instruction in instructions:
            opcode = instruction["opcode"]
            if opcode in (0x12, 0x13, 0x14, *range(0xB2, 0xBB), 0xBB, 0xBD, 0xC0, 0xC1, 0xC5):
                constant = self.constant(method["class_index"], instruction["operands"][0])
                instruction["constant"] = constant
                value = constant["value_raw"]
                if 0xB6 <= opcode <= 0xB9:
                    if value >= len(self.methods):
                        raise ValueError("JPC invocation refers to an invalid method")
                    instruction["method"] = self.methods[value]
                elif 0xB2 <= opcode <= 0xB5:
                    if value >= len(self.fields_raw):
                        raise ValueError("JPC field instruction refers to an invalid field")
                    instruction["field_index"] = value
                elif opcode in (0xBB, 0xBD, 0xC0, 0xC1, 0xC5):
                    if value >= len(self.classes):
                        raise ValueError("JPC instruction refers to an invalid class")
                    instruction["class_name"] = self.classes[value]["name"]
        return instructions


def _integer(instruction):
    opcode = instruction["opcode"]
    if 0x02 <= opcode <= 0x08:
        return opcode - 3
    if opcode in (0x10, 0x11):
        return instruction["operands"][0]
    if opcode in (0x12, 0x13):
        return instruction["constant"]["signed_value"]
    raise ValueError("Portal script argument is not a literal integer")


def _calls(instruction, class_name, method_name):
    method = instruction.get("method", {})
    return method.get("class_name") == class_name and method.get("name") == method_name


def _successors(instructions):
    result = {}
    for index, instruction in enumerate(instructions):
        opcode = instruction["opcode"]
        following = [instructions[index + 1]["offset"]] if index + 1 < len(instructions) else []
        if opcode in (0xA8, 0xA9, 0xC9) or instruction.get("wide_opcode") == 0xA9:
            raise ValueError("Subroutines are not supported in portal policy analysis")
        if opcode in (0xA7, 0xC8):
            edges = [instruction["target"]]
        elif opcode in (0xAA, 0xAB):
            edges = [instruction["default"], *(pair[1] for pair in instruction["cases"])]
        elif 0xAC <= opcode <= 0xB1 or opcode == 0xBF:
            edges = []
        elif "target" in instruction:
            edges = [*following, instruction["target"]]
        else:
            edges = following
        result[instruction["offset"]] = edges
    return result


def _dominates_returns(instructions, offset):
    successors = _successors(instructions)
    pending = [instructions[0]["offset"]]
    seen = set()
    returns = {item["offset"] for item in instructions if 0xAC <= item["opcode"] <= 0xB1}
    while pending:
        current = pending.pop()
        if current == offset or current in seen:
            continue
        if current in returns:
            return False
        seen.add(current)
        pending.extend(successors[current])
    return True


def extract_portal_policy(package: ScriptPackage, scene_class: str, selected_portals) -> dict:
    """Extract the verified AK01/AK09 door idiom, failing on a changed branch.

    This is an instruction-pattern reader, not a JVM. It preserves source branch
    order. Earlier allowUse branches can take priority; later code can change the
    assigned action. It does not claim to evaluate the complete actor state machine.
    """
    selected = set(selected_portals)
    if not selected or any(not isinstance(value, int) or value < 0 for value in selected):
        raise ValueError("Portal selection must contain nonnegative integer indices")

    def method(name):
        found = [item for item in package.methods if item["class_name"] == scene_class and item["name"] == name]
        if len(found) != 1:
            raise ValueError(f"Expected one {scene_class}.{name} method")
        return found[0]["index"]

    activate, allow = method("activateScene"), method("allowUse")
    activation = package.disassemble(activate)
    disabled = {}
    for index, item in enumerate(activation):
        is_disable = _calls(item, "ed/base/EDScene", "portalDisable")
        is_enable = _calls(item, "ed/base/EDScene", "portalEnable")
        if (is_disable or is_enable) and index:
            portal = _integer(activation[index - 1])
            if portal in selected:
                if is_enable or not _dominates_returns(activation, item["offset"]):
                    raise ValueError("Selected portal does not have unconditional disabled activation state")
                disabled[portal] = item["offset"]
    if set(disabled) != selected:
        raise ValueError("Missing unconditional selected-portal disable")

    instructions = package.disassemble(allow)
    _successors(instructions)
    gates = []
    for index, item in enumerate(instructions):
        if not _calls(item, "GenericScene", "portalEnableSoundPlay"):
            continue
        if index < 2:
            raise ValueError("Incomplete portal helper arguments")
        portal = _integer(instructions[index - 2])
        if portal not in selected:
            continue
        block = instructions[index - 13:index + 1]
        if len(block) != 14:
            raise ValueError("Incomplete door interaction branch")
        if (any(block[position]["opcode"] != 0x2B for position in (0, 4))
                or not _calls(block[2], "ed/base/EDActor", "inTrigger")
                or not _calls(block[7], "ed/base/EDActor", "facingMarker")
                or any(block[position]["opcode"] != 0x99 for position in (3, 8))
                or block[10]["opcode"] != 0x3D
                or block[12]["opcode"] not in (0x12, 0x13) or item["opcode"] != 0xB8
                or any(block[position]["target"] <= item["offset"] for position in (3, 8))):
            raise ValueError("Unsupported door interaction branch; do not infer missing conditions")
        # Explicit jumps into this block would bypass its source predicates.
        inner = {entry["offset"] for entry in block[1:]}
        explicit_targets = {entry["target"] for entry in instructions if "target" in entry}
        explicit_targets.update(target for entry in instructions for _, target in entry.get("cases", []))
        explicit_targets.update(entry["default"] for entry in instructions if "default" in entry)
        if inner & explicit_targets:
            raise ValueError("A jump bypasses a door interaction predicate")
        gates.append({"portal_index": portal, "trigger_index": _integer(block[1]),
                      "marker_index": _integer(block[5]), "angle_argument_degrees": _integer(block[6]),
                      "sound_resource_id": block[12]["constant"]["value_raw"],
                      "action_assigned": _integer(block[9]),
                      "source_offsets": {"branch": block[0]["offset"], "in_trigger": block[2]["offset"],
                                         "facing_marker": block[7]["offset"], "pending_portal": item["offset"]}})
    if {gate["portal_index"] for gate in gates} != selected or len(gates) != len(selected):
        raise ValueError("Expected exactly one source interaction branch per selected portal")
    return {"scene_class": scene_class, "activate_method": activate, "allow_use_method": allow,
            "initially_disabled": sorted(disabled), "disable_call_offsets": disabled, "gates": gates}


def source_facing_allows(delta_radians: float, angle_argument_degrees: int) -> bool:
    """Reference for native 0x4047A0, including its float-to-integer abs quirk."""
    if not math.isfinite(delta_radians):
        raise ValueError("Non-finite facing difference")
    radians = abs(angle_argument_degrees) * 0.01745329238474369
    if radians >= 3.1415927410125732:
        return True
    wrapped = (delta_radians + math.pi) % (2 * math.pi) - math.pi
    return abs(math.trunc(wrapped)) < radians


def extract_portal_timing(package: ScriptPackage) -> dict:
    """Read and validate the source sound-delay and input-guard instruction idioms."""
    def named(owner, name):
        methods = [m for m in package.methods if m["class_name"] == owner and m["name"] == name]
        if len(methods) != 1:
            raise ValueError(f"Expected one {owner}.{name}")
        return methods[0]["index"], package.disassemble(methods[0]["index"])

    def field(instruction):
        if instruction["opcode"] not in (0xB2, 0xB3):
            raise ValueError("Expected a source static field instruction")
        return instruction["constant"]["value_raw"]

    helper_id, helper = named("GenericScene", "portalEnableSoundPlay")
    expected = ["iload_0", "putstatic", "iconst_1", "putstatic", "iload_1", "invokestatic",
                "putstatic", "getstatic", "invokestatic", "putstatic", "bipush", "putstatic",
                "iconst_1", "putstatic", "bipush", "putstatic", "return"]
    if ([i["mnemonic"] for i in helper] != expected
            or not _calls(helper[5], "ed/base/EDBase", "getSNDID")
            or not _calls(helper[8], "ed/base/EDBase", "soundPlay")
            or field(helper[6]) != field(helper[7])):
        raise ValueError("Unsupported source portal sound helper")
    portal, pending, voice, countdown, guard, guard_counter = [field(helper[i]) for i in (1, 3, 9, 11, 13, 15)]
    delay, guard_ticks = _integer(helper[10]), _integer(helper[14])

    frame_id, frame = named("GenericScene", "soundTrackProcessFrame")
    starts = [index for index, item in enumerate(frame) if item["opcode"] == 0xB2 and field(item) == pending]
    if len(starts) != 1:
        raise ValueError("Missing source pending portal frame branch")
    block = frame[starts[0]:starts[0] + 16]
    if ([item["mnemonic"] for item in block] != ["getstatic", "ifeq", "getstatic", "iconst_m1", "iadd",
            "putstatic", "getstatic", "invokestatic", "ifne", "getstatic", "ifle", "getstatic",
            "bipush", "if_icmpgt", "iconst_0", "putstatic"]
            or any(field(block[index]) != countdown for index in (2, 5, 9, 11, 15))
            or field(block[6]) != voice or not _calls(block[7], "ed/base/EDBase", "isVoicePlaying")
            or block[8]["target"] != block[11]["offset"] or block[10]["target"] != block[14]["offset"]
            or block[13]["target"] != block[1]["target"]):
        raise ValueError("Unsupported source delayed portal frame branch")
    lower_limit = _integer(block[12])
    tail = [item for item in frame if block[14]["offset"] <= item["offset"] < block[1]["target"]]
    enables_pending = any(index and _calls(item, "ed/base/EDScene", "portalEnable")
                          and tail[index - 1]["opcode"] == 0xB2 and field(tail[index - 1]) == portal
                          for index, item in enumerate(tail))
    clears_pending = (len(tail) >= 2 and tail[-2]["opcode"] == 0x03
                      and tail[-1]["opcode"] == 0xB3 and field(tail[-1]) == pending)
    main_id, main = named("Main", "processFrame")
    guards = [index for index, item in enumerate(main) if item["opcode"] == 0xB2 and field(item) == guard]
    if len(guards) != 1:
        raise ValueError("Missing source main input guard")
    main_block = main[guards[0]:guards[0] + 10]
    if ([item["mnemonic"] for item in main_block] != ["getstatic", "ifeq", "getstatic", "iconst_m1",
            "iadd", "putstatic", "getstatic", "ifgt", "iconst_0", "putstatic"]
            or any(field(main_block[index]) != guard_counter for index in (2, 5, 6))
            or field(main_block[9]) != guard or main_block[1]["target"] != main_block[7]["target"]
            or not enables_pending or not clears_pending or delay <= 0 or lower_limit >= 0 or guard_ticks <= 0):
        raise ValueError("Unsupported source input guard or portal completion")
    return {"helper_method": helper_id, "sound_frame_method": frame_id, "main_frame_method": main_id,
            "delay_ticks": delay, "forced_enable_countdown": lower_limit,
            "latest_enable_ticks": delay - lower_limit, "input_guard_ticks": guard_ticks,
            "fields": {"pending_portal": portal, "pending_active": pending, "voice": voice,
                       "countdown": countdown, "input_guard": guard, "input_guard_counter": guard_counter}}
