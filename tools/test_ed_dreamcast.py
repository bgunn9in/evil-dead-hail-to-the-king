from pathlib import Path
import struct
import tempfile
import unittest
from ed_assets import DC_DATA_OFFSET
from ed_dreamcast import classify,inventory


class DreamcastInventoryTests(unittest.TestCase):
    def test_scoped_duplicate_ids_keep_distinct_payload_hashes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            for name,payload in (("AA01.DC",b"one\0"),("AK01.DC",b"two\0")):
                data=bytearray(DC_DATA_OFFSET);struct.pack_into("<6I",data,0,0x61610100,0x30004,0,20,0,1)
                struct.pack_into("<4I",data,24,0x31800800,0,DC_DATA_OFFSET,len(payload));data.extend(payload);(root/name).write_bytes(data)
            result=inventory(root)
            self.assertEqual((result["archive_count"],result["resource_count"]),(2,2))
            records=[a["entries"][0] for a in result["archives"]]
            self.assertEqual([r["id"] for r in records],["31800800"]*2)
            self.assertNotEqual(records[0]["sha256"],records[1]["sha256"])
            self.assertTrue(all(r["ascii"] and r["text_bytes"]==3 for r in records))

    def test_unknown_payload_not_reinterpreted_and_empty_rejected(self):
        self.assertEqual(classify(0x12345678,9),"unknown")
        self.assertEqual(classify(0x31800800,0),"text")
        self.assertEqual(classify(0x0b942034,1),"texture")
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaises(ValueError):inventory(Path(temporary))

    def test_actual_140_archive_fixture_when_available(self):
        source=Path(__file__).resolve().parents[1]/"build/dreamcast-original-20261027/files"
        if not source.is_dir():self.skipTest("Extracted original Dreamcast disc required")
        result=inventory(source)
        self.assertEqual(result["archive_count"],140)
        self.assertEqual(result["counts"]["text"],835)
        self.assertTrue(all(r["ascii"] for a in result["archives"] for r in a["entries"] if r["kind"]=="text"))


if __name__=="__main__":unittest.main()
