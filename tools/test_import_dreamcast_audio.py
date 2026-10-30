import unittest
from pathlib import Path
from import_dreamcast_audio import select_audio, relative_clip


def bank(archive, entry, ids):
    return {"archive": archive, "entry": entry, "flags": 1, "id": 0x0d990000,
            "clips": [{"slot": n, "id": identity, "data": archive + ":" + str(entry)} for n, identity in enumerate(ids)]}


def manifest(extra):
    banks = [bank("AZ01.DC", 0, [10]), bank("BZ01.DC", 0, [20])] + extra
    return {"archives": [{"archive": a} for a in ("AZ01.DC", "BZ01.DC", "AH02.DC", "AH02V.DC")], "banks": banks}


class ImportAudioTests(unittest.TestCase):
    def test_global_duplicate_keeps_protected_global_ownership(self):
        result, skipped = select_audio(manifest([bank("AH02.DC", 0, [10, 30])]), ["ah02"], "a")
        self.assertEqual([c["id"] for _, c in result["ah02"]], [30])
        self.assertEqual(skipped[0]["resident_archive"], "AZ01.DC")
        self.assertEqual(result["global"][0][1]["data"], "AZ01.DC:0")

    def test_native_physical_entry_order_before_vdc(self):
        result, skipped = select_audio(manifest([bank("AH02V.DC", 0, [30, 40]),
            bank("AH02.DC", 9, [30]), bank("AH02.DC", 1, [30])]), ["ah02"], "a")
        self.assertEqual([(b["archive"], b["entry"], c["id"]) for b, c in result["ah02"]],
                         [("AH02.DC", 1, 30), ("AH02V.DC", 0, 40)])
        self.assertEqual(len(skipped), 2)

    def test_inactive_package_global_does_not_mask_scene_id(self):
        result, _ = select_audio(manifest([bank("AH02.DC", 0, [20])]), ["ah02"], "a")
        self.assertEqual([c["id"] for _, c in result["ah02"]], [20])

    def test_native_resident_limit_and_level_boundary(self):
        with self.assertRaises(ValueError):
            select_audio(manifest([bank("AH02.DC", 0, list(range(1000, 1096)))]), ["ah02"], "a")
        with self.assertRaises(ValueError):
            select_audio(manifest([]), ["bh02"], "a")

    def test_decoded_files_cannot_escape_inventory(self):
        root = Path.cwd().resolve()
        for value in ("../escaped.wav", "C:/escaped.wav", r"clips\escaped.wav"):
            with self.assertRaises(ValueError):
                relative_clip(root, value)


if __name__ == "__main__":
    unittest.main()
