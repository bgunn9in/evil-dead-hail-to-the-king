import hashlib
from pathlib import Path
import struct
import tempfile
import unittest

from compile_localization import font_charset, inventory_label, original_text_keys, pack_inventory, pack_text, translations


def catalog(english="Name", russian="Имя"):
    return {"format": "ED-TRANSLATION-AUTHORING1", "runtime_encoding": "windows-1251", "groups": [{
        "key": hashlib.sha256(english.encode("ascii")).hexdigest(), "en": english, "ru": russian,
        "controls": [], "sources": [{"level": "A", "scope": "00000000", "id": "31990960"}]}]}


def original_text():
    return struct.pack("<8s4I", b"EGTEXT2\0", 1, 0, 0x31990960, 5) + b"Name\0"


class CompileTests(unittest.TestCase):
    def test_exact_keys_and_declared_cp1251(self):
        strings = translations(catalog(), "A", set("Имя".encode("cp1251")))
        packed = pack_text(strings, original_text())
        self.assertEqual(struct.unpack_from("<8s2I", packed), (b"EGTEXT3\0", 1251, 1))
        self.assertEqual(packed[-4:], "Имя".encode("cp1251") + b"\0")
        with self.assertRaises(ValueError):
            pack_text({}, original_text())
        with self.assertRaises(ValueError):
            original_text_keys(original_text() + b"x")

    def test_reject_missing_glyph_and_altered_original(self):
        with self.assertRaises(ValueError):
            translations(catalog(), "A", {ord("A")})
        wrong = catalog()
        wrong["groups"][0]["en"] = "Changed"
        with self.assertRaises(ValueError):
            translations(wrong, "A", set(range(32, 256)))

    def test_controls_not_rendered_and_braces_preserved(self):
        value = catalog("$A032048128{Yes}$C", "$A032048128{Да}$C")
        value["groups"][0]["controls"] = ["$A032048128", "$C"]
        translations(value, "A", set("{}Да".encode("cp1251")))
        value["groups"][0]["ru"] = "$A032048128Да$C"
        with self.assertRaises(ValueError):
            translations(value, "A", set("Да".encode("cp1251")))

    def test_font_requires_explicit_declaration_and_fits_atlas(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "font.txt"
            for width, height in ((256, 512), (512, 1024)):
                path.write_bytes(f"EDFONT2\n18 24 14 {width} {height}\nCP1251\n".encode("ascii") + "АЯ".encode("cp1251") + b"\n")
                self.assertEqual(font_charset(path), set("АЯ".encode("cp1251")))
            for dimensions in ("18 24 14 512 256", "18 24 14 512 768", "18 24 14 1024 2048", "36 48 14 512 1024"):
                path.write_bytes(f"EDFONT2\n{dimensions}\nCP1251\nABC\n".encode("ascii"))
                with self.assertRaises(ValueError):
                    font_charset(path)
            path.write_bytes(b"EDFONT1\n18 24 14\nABC\n")
            with self.assertRaises(ValueError):
                font_charset(path)

    def test_inventory_keeps_original_visual_fields(self):
        texture = struct.pack("<3I", 0x0b942244, 512, 512)
        visual = struct.pack("<4I5f", 119, 60, 0, 12, 0, 0, 1, 1, 1.28)
        original = struct.pack("<8s2I", b"EDINV1\0\0", 1, 1) + texture + visual + struct.pack("<2I", 4, 4) + b"NameDesc"
        strings = {(0, 1): "Имя".encode("cp1251"), (0, 2): "Текст".encode("cp1251")}
        packed = pack_inventory(original, strings, lambda ids: {119: (1, 2)})
        self.assertEqual(struct.unpack_from("<8s3I", packed), (b"EDINV2\0\0", 1251, 1, 1))
        self.assertEqual(packed[20:68], texture + visual)
        self.assertEqual(packed[76:], "ИмяТекст".encode("cp1251"))
        self.assertEqual(pack_inventory(packed,strings,lambda ids:{119:(1,2)}),packed)
        changed=dict(strings);changed[(0,2)]="Новый\nтекст".encode('cp1251')
        revised=pack_inventory(packed,changed,lambda ids:{119:(1,2)})
        self.assertEqual(revised[:68],packed[:68])
        self.assertTrue(revised.endswith("ИмяНовый\nтекст".encode('cp1251')))
        bad=bytearray(packed);struct.pack_into('<I',bad,8,65001)
        with self.assertRaises(ValueError):pack_inventory(bad,strings,lambda ids:{119:(1,2)})
        with self.assertRaises(ValueError):
            pack_inventory(original + b"x", strings, lambda ids: {119: (1, 2)})

    def test_inventory_plain_labels_keep_source_gui_stream_separate(self):
        raw = "$A032048128Текст$C@@Эш^s".encode("cp1251")
        self.assertEqual(inventory_label(raw), "Текст\n\nЭш's".encode("cp1251"))
        self.assertIn(b"$A032048128", raw)


if __name__ == "__main__":
    unittest.main()
