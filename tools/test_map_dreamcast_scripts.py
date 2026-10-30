from pathlib import Path
import struct
import tempfile
import unittest

from ed_scripts import ScriptPackage
from map_dreamcast_scripts import ROOT, class_keys, compare, cpp_inventory, field_slots, normalized_code


def package(reordered=False, wide=False, field_count=2, field_target=0, number=7, literal_padding=None):
    strings=bytearray()
    def text(value):
        at=len(strings);strings.extend(value.encode()+b"\0");return at
    owners=["Other","java/lang/String","ed/base/External","Core"] if reordered else ["Core","java/lang/String","Other","ed/base/External"]
    names={name:text(name) for name in owners}
    definitions=[("engineCall","(I)I","ed/base/External"),("f","()I","Core")] if reordered else [("f","()I","Core"),("engineCall","(I)I","ed/base/External")]
    method_names=[(text(name),text(signature),owner) for name,signature,owner in definitions]
    indices={owner:index for index,owner in enumerate(owners)}
    fields=[];ranges={}
    for owner in owners:
        count=field_count if owner=="Core" else 1 if owner=="Other" else 0
        ranges[owner]=(len(fields),count)
        fields.extend([[-12851,-12851,0,1] for _ in range(count)])
    method_index=next(index for index,row in enumerate(definitions) if row[2]=="ed/base/External")
    values=[0,ranges["Core"][0]+field_target,method_index,indices["Other"],number]
    pool=[1,2,3,4]
    code=bytearray(b"\xbb\0\2\x57") # Typed class reference, then discard.
    code += b"\x13\0\3" if wide else b"\x12\3"
    code += bytes.fromhex("99 000a b2 0000 b8 0001 ac 03 ac")
    heap=b""
    if literal_padding is not None:
        pad=literal_padding;image=bytearray(pad+48)
        struct.pack_into("<3I",image,pad,0,1,4);image[pad+12:pad+16]=b"abc\0"
        struct.pack_into("<6I",image,pad+24,1,4,3,pad+12,0,3)
        values[4]=pad+36;pool[3]=-4;heap=struct.pack("<I",len(image))+image
    methods=[]
    for index,(name,signature,owner) in enumerate(method_names):
        native=owner=="ed/base/External"
        methods.append(struct.pack("<10hI",name,signature,0x109 if native else 9,0,-12851,indices[owner],0,3,0,
                                   0 if native else len(code),0xffffffff if native else 0))
    classes=[]
    for index,owner in enumerate(owners):
        own=[i for i,row in enumerate(definitions) if row[2]==owner]
        first,count=ranges[owner]
        classes.append(struct.pack("<10h",names[owner],0,0,0,own[0] if own else 0,first,len(own),count,-1,index))
    header=struct.pack("<14I",len(fields),len(methods),len(classes),len(code),len(strings),len(values),len(pool),*([0]*7))
    return ScriptPackage(header+code+b"".join(methods)+b"".join(struct.pack("<4h",*row) for row in fields)+b"".join(classes)+strings+
                         struct.pack("<4h",*pool)+struct.pack("<5I",*values)+heap)


class MapTests(unittest.TestCase):
    def test_typed_operands_and_width_relocation_are_equal(self):
        result=compare(package(),package(reordered=True,wide=True))
        method=next(row for row in result["methods"] if row["symbol"]==("Core","f","()I"))
        self.assertEqual((method["pc"],method["dc"]),(0,1))
        self.assertEqual(method["status"],"resolved_image_equal")
        self.assertNotEqual(method["pc_code_sha256"],method["dc_code_sha256"])

    def test_numeric_literals_and_real_field_changes_are_not_redirected(self):
        for other in (package(number=8),package(field_target=1)):
            method=next(row for row in compare(package(),other)["methods"] if row["symbol"][0]=="Core")
            self.assertEqual(method["status"],"changed")

    def test_changed_owner_layout_requires_aligned_access_evidence(self):
        result=compare(package(field_count=3,field_target=1),package(field_count=2,field_target=0))
        core=next(row for row in result["methods"] if row["symbol"][0]=="Core")
        self.assertEqual(core["status"],"resolved_image_equal")
        pair=next(row for row in result["fields"] if row["pc"]==1)
        self.assertEqual((pair["dc"],pair["status"]),(0,"aligned_access_bijection"))
        self.assertTrue(any(row["status"]=="unresolved_owner_layout_changed" for row in result["fields"]))

    def test_literal_heap_relocation_keeps_actual_string_image(self):
        a,b=package(literal_padding=0),package(literal_padding=8)
        self.assertEqual(normalized_code(a,a.methods[0],field_slots(a)),normalized_code(b,b.methods[0],field_slots(b)))

    def test_duplicate_class_names_remain_explicitly_positional(self):
        value=package()
        value.classes[2]["name"]="Core"
        self.assertEqual((class_keys(value)[0],class_keys(value)[2]),("Core#declaration_occurrence0","Core#declaration_occurrence1"))
        result=compare(value,value)
        self.assertTrue(next(row for row in result["methods"] if row["symbol"][1]=="f")["duplicate_owner_name_positional"])

    def test_lexical_inventory_masks_comments_strings_and_keeps_indirects(self):
        with tempfile.TemporaryDirectory() as folder:
            Path(folder,"core.h").write_text('''// vm.field(ref,999)
const char* note="vm.staticField(998)";
vm.field(ref,13);vm.staticField(844);vm.invoke(pkg.methodIndex("A","f","()V"));
vm.field(ref,uint32_t(container));vm.statics[310];
''',encoding="utf-8")
            result=cpp_inventory(folder)
            self.assertEqual(result["summary"]["production"]["calls"],6)
            self.assertEqual(result["summary"]["production"]["numeric_calls"],3)
            self.assertEqual(sorted(row["index_literal"] for row in result["sites"] if row["index_literal"] is not None),[13,310,844])

    @unittest.skipUnless((ROOT/"build/dreamcast-original-20261027/files/LEVELA.JDC").exists(),"Requires original Dreamcast packages")
    def test_original_native_declarations_and_entrypoints(self):
        for level,count in (("A",2060),("B",1997)):
            dc=ScriptPackage((ROOT/f"build/dreamcast-original-20261027/files/LEVEL{level}.JDC").read_bytes())
            self.assertEqual(len(dc.methods),count)
            self.assertTrue(all(row[:2]==[-12851,-12851] for row in dc.fields_raw))
            slots=field_slots(dc)
            self.assertEqual(slots[995][0],"Inventory")
            self.assertEqual([k for k in class_keys(dc) if k.startswith("Chainsaw#")],
                             ["Chainsaw#declaration_occurrence0","Chainsaw#declaration_occurrence1"])
            for name in ("initialize","createScene","savePlayer","restorePlayer","processFrame"):
                method=next(row for row in dc.methods if row["class_name"]=="Main" and row["name"]==name)
                self.assertTrue(normalized_code(dc,method,slots))


if __name__=="__main__":
    unittest.main()
