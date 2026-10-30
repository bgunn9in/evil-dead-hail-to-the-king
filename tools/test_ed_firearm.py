from pathlib import Path
import struct
import unittest
from ed_firearm import audit_firearms

GAME=Path(__file__).resolve().parents[1]/"game"

@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class FirearmAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.audit=audit_firearms(GAME)
    def test_all_ash_records(self):
        self.assertEqual(self.audit["all_raw36_counts"],{0:15,2:27,6:9,7:5,8:6,9:52,20:177,22:3,35:12})
        shots=[r for r in self.audit["firearm_records"]if r["kind"]==7]
        self.assertEqual({r["clip"]for r in shots},{0x01825188,0x01825208,0x01825288})
        self.assertTrue(all((r["frame"],r["enabled"],r["mode"],r["trailing"])==(1,1,-1,0)for r in shots))
        self.assertEqual(len([r for r in shots if r["clip"]==0x01825288]),3)
    def test_exact_source_offsets(self):
        blob=(GAME/"data/az01.pc").read_bytes()
        for record in self.audit["firearm_records"]:
            at=record["archive_offset"];self.assertEqual(blob[at:at+36].hex(),record["raw"])
            self.assertEqual(struct.unpack_from("<HBBi",blob,at),(record["frame"],record["kind"],record["enabled"],record["mode"]))
        self.assertEqual(len(self.audit["packs"]),2)
    def test_source_methods(self):
        methods={m["index"]:m for m in self.audit["methods"]}
        self.assertEqual((methods[1292]["owner"],methods[1292]["name"]),("InvWeapon","itemUse"))
        self.assertEqual(methods[1309]["descriptor"],"(IZ)Z")
        self.assertEqual((methods[1597]["owner"],methods[1597]["name"]),("GenericNpc","doDamageCalculation"))

if __name__=="__main__":unittest.main()
