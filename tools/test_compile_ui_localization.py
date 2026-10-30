import json
from pathlib import Path
import tempfile
import unittest

from compile_ui_localization import generate


class UICompileTests(unittest.TestCase):
    def test_reproducible_declared_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/"font.txt").write_bytes(b"EDFONT2\n18 24 14 256 512\nCP1251\n"+"Пауза".encode("cp1251")+b"\n")
            (root/"ui.json").write_text(json.dumps({"format":"ED-HOST-UI-RU1","entries":{"paused":"Пауза"}},ensure_ascii=False),encoding="utf-8")
            self.assertEqual(generate(root/"ui.json",root/"font.txt",root/"one.h"),1)
            (root/"font.txt").write_bytes(b"EDFONT2\n18 24 14 512 1024\nCP1251\n"+"Пауза".encode("cp1251")+b"\n")
            generate(root/"ui.json",root/"font.txt",root/"two.h")
            self.assertEqual((root/"one.h").read_bytes(),(root/"two.h").read_bytes())
            self.assertIn(b"\\317\\340\\363\\347\\340",(root/"one.h").read_bytes())

    def test_duplicate_missing_glyph_and_overlong_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/"font.txt").write_bytes(b"EDFONT2\n18 24 14 256 512\nCP1251\nA\n")
            for text in ('{"format":"ED-HOST-UI-RU1","entries":{"x":"A","x":"A"}}',
                         json.dumps({"format":"ED-HOST-UI-RU1","entries":{"x":"Я"}}),
                         json.dumps({"format":"ED-HOST-UI-RU1","entries":{"x":"A"*513}})):
                (root/"ui.json").write_text(text,encoding="utf-8")
                with self.assertRaises(ValueError):
                    generate(root/"ui.json",root/"font.txt",root/"out.h")
                self.assertFalse((root/"out.h").exists())


if __name__=="__main__":
    unittest.main()
