from pathlib import Path
import struct
import tempfile
import unittest

from ed_actor_textures import canonical,read_actor_textures,export_actor_textures,VARIANT_ACTORS
from export_actor import ACTORS

GAME=Path(__file__).resolve().parents[1]/"game"
EXPECTED_IDS={canonical(actor["texture"])for actor in ACTORS.values()}|set(VARIANT_ACTORS)


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class ActorTextureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.assets=read_actor_textures(GAME)

    def test_complete_jpc_callers_and_flavors(self):
        for package in self.assets["packages"]:
            self.assertEqual(len(package["callers"]),11)
            self.assertEqual([c["method"]for c in package["native_callers"]],[105])
            variants=[c for c in package["callers"]if not c["default_reset"]]
            self.assertEqual(len(variants),9)
            self.assertEqual({c["resource_id"]for c in variants},set(VARIANT_ACTORS))
            self.assertEqual(sorted(f for c in variants for f in c["flavors"]),[10,11,21,22,23,25,28,29,44])

    def test_real_dds_and_stub_rejection(self):
        self.assertEqual(self.assets["archive_count"],140)
        self.assertEqual({texture["resource_id"]for texture in self.assets["textures"]},EXPECTED_IDS)
        for texture in self.assets["textures"]:
            self.assertEqual((texture["width"],texture["height"],texture["texture"].mode),(256,256,"RGBA"))
            self.assertTrue(all(source["size"]>136 and source["flags"]==1 for source in texture["sources"]))
            self.assertFalse(any(source["archive"]=="az00.pc" for source in texture["sources"]))
        ash=next(t for t in self.assets["textures"]if t["resource_id"]==0x08024000)
        self.assertTrue(any(s["archive"]=="az00.pc" and s["size"]==132 for s in ash["rejected_sources"]))

    def test_export_identity_pixels_alpha_and_original_guard(self):
        with tempfile.TemporaryDirectory() as directory:
            metadata=export_actor_textures(GAME,Path(directory))
            target=Path(directory)/"actors/textures"
            lines=(target/"manifest.txt").read_text().splitlines();self.assertEqual(lines[:2],["EDACTORTEX1",str(len(EXPECTED_IDS))])
            self.assertEqual(lines[2:],[f"{t['resource_id']:08x}"for t in metadata["textures"]])
            for texture,source in zip(metadata["textures"],self.assets["textures"]):
                raw=(target/f"{texture['resource_id']:08x}.rgba").read_bytes()
                self.assertEqual(struct.unpack_from("<8s3I",raw),(b"EDTEX1\0\0",texture["resource_id"],256,256))
                self.assertEqual(raw[20:],source["texture"].tobytes())
        with self.assertRaises(ValueError):export_actor_textures(GAME,GAME)

    def test_native_mask_preserves_variant_bits(self):
        self.assertEqual(canonical(0x880c6004),0x080c6004)
        self.assertNotEqual(canonical(0x080c6004),canonical(0x080c6000))
        self.assertEqual(canonical(0x08198008),0x08198008)
        with self.assertRaises(ValueError):canonical(-1)

    def test_unknown_executable_version_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory)/"EvilDead.exe").write_bytes(b"unverified executable fixture")
            with self.assertRaises(ValueError):read_actor_textures(Path(directory))


if __name__=="__main__":unittest.main()
