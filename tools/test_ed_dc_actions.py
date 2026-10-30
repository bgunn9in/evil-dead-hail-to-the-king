from pathlib import Path
import unittest

from ed_dc_actions import IMAGE_BASE, TABLES, parse_dc_actor_actions, read_image
from export_actor import ACTORS, LEVEL_B_ACTORS

ROOT = Path(__file__).resolve().parents[1]


class DreamcastActionTests(unittest.TestCase):
    def test_executable_identity_and_image_bounds(self):
        with self.assertRaises(ValueError):
            parse_dc_actor_actions(b"unverified", b"")
        self.assertEqual(read_image(b"abcdef", IMAGE_BASE + 1, 3), b"bcd")
        self.assertEqual(read_image(b"abcdef", IMAGE_BASE + 6, 0), b"")
        for address, size in ((IMAGE_BASE - 1, 0), (IMAGE_BASE, -1), (IMAGE_BASE + 7, 0), (IMAGE_BASE + 5, 2)):
            with self.subTest(address=address, size=size), self.assertRaises(ValueError):
                read_image(b"abcdef", address, size)

    @unittest.skipUnless((ROOT / "build/dreamcast-original-20261027/files/1ST_READ.BIN").is_file(), "Requires original Dreamcast image")
    def test_every_actual_actor_and_exact_native_masks(self):
        source = ROOT / "build/dreamcast-original-20261027/files"
        executable = (source / "1ST_READ.BIN").read_bytes()
        definitions = {**ACTORS, **LEVEL_B_ACTORS}
        seen = set()
        for actor, definition in definitions.items():
            action_actor = definition.get("action_actor", actor)
            archive_name = Path(definition["archive"]).stem
            archive = (source / (archive_name.upper() + ".DC")).read_bytes()
            states = parse_dc_actor_actions(executable, archive, action_actor)
            self.assertTrue(states)
            seen.add(action_actor)
            if actor in ("demonpig", "treeboss"):
                # Native masks include the original adjacent SH4 image bytes after bone17.
                expected = ([1] * 17 + [0, 3, 0, 4, 0, 24, 0, 2] if actor == "demonpig" else
                            [1] * 17 + [0, 46, 0, 1, 0, 48, 0, 2, 0, 50, 0, 3, 0, 41, 0, 4,
                                        0, 64, 0, 5, 0, 65, 0, 5, 0, 66, 0, 5])
                for current in states:
                    self.assertEqual(current["bone_mask"], expected)
            self.assertEqual(states[0]["source_va"], TABLES[action_actor])
        self.assertEqual(seen, set(TABLES))


if __name__ == "__main__":
    unittest.main()
