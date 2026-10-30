import unittest

from export_localization_catalog import grouped_catalog, text_payload, validate_translation


class CatalogTests(unittest.TestCase):
    def test_padded_dc_text(self):
        self.assertEqual(text_payload(b"New Game\0\0\0\0"), "New Game")
        with self.assertRaises(ValueError):
            text_payload(b"New\0Game\0")

    def test_scope_and_duplicate_text(self):
        rows = []
        for scope, text in (("616b0100", "First"), ("61630100", "Other"), ("00000000", "First")):
            rows.append(dict(level="A", scope=scope, scope_name=scope, id="91820800",
                             archive="test.DC", entry=0, offset=16384, bytes=len(text)+1,
                             sha256="source", text=text))
        value = grouped_catalog(rows)
        self.assertEqual(value["summary"]["scoped_keys"], 3)
        self.assertEqual(value["summary"]["unique_english"], 2)
        self.assertEqual(sum(len(g["sources"]) for g in value["groups"]), 3)

    def test_translation_controls_and_encoding(self):
        group = {"controls": ["$A255000000", "$C", "@"]}
        text = "$A255000000Красный$C@Текст"
        self.assertEqual(validate_translation(group, text).decode("cp1251"), text)
        with self.assertRaises(ValueError):
            validate_translation(group, "Текст")
        with self.assertRaises(UnicodeEncodeError):
            validate_translation({"controls": []}, "Текст🙂")


if __name__ == "__main__":
    unittest.main()
