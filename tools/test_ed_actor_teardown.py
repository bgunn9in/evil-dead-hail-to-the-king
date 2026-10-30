from pathlib import Path
import unittest

from ed_actor_teardown import audit
from ed_scripts import ScriptPackage


GAME = Path(__file__).resolve().parents[1] / "game"


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class ActorTeardownTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.executable = (GAME / "EvilDead.exe").read_bytes()
        cls.package = ScriptPackage((GAME / "data/script/pc/levelA.jpc").read_bytes())

    def test_original_context_descriptor_and_registration_limits(self):
        proof = audit(self.executable, self.package)
        self.assertEqual(proof["unregister_descriptor_types"], list(range(5, 40)))
        self.assertEqual(proof["physical_actor_slots"], 13)
        self.assertEqual([c["method"] for c in proof["level_create_calls"]], [640, 118, 65, 118, 641, 118, 65, 642, 118, 65])

    def test_original_end_scene_and_re_registration_order(self):
        proof = audit(self.executable, self.package)
        self.assertEqual([c["method"] for c in proof["end_scene_calls"]], [485, 901, 934, 796, 808, 779, 1785, 1656, 972, 639, 844])
        self.assertEqual(proof["persistent_message_registration_pcs"], [763, 781])

    def test_different_executable_is_not_silently_given_this_semantics(self):
        changed = bytearray(self.executable);changed[0x1CFDF] ^= 1
        with self.assertRaises(ValueError):
            audit(bytes(changed), self.package)
