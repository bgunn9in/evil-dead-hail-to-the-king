from collections import Counter
import hashlib
from pathlib import Path
import shutil
import struct
import tempfile
import unittest

from ed_dc_audio import (ROOT, checked_output, dc_sound_bank, decode_aica, derived_str,
                         export_dc_audio, inventory_dc_audio, inventory_metadata)

SOURCE = ROOT / "build/dreamcast-original-20261027/files"


class DreamcastSoundTests(unittest.TestCase):
    def test_low16_byte_count_and_separate_nonzero_rate_flag(self):
        data = struct.pack("<IIIHH", 1, 0xEE9221ED, 16, 4, 0xFFFF) + b"\x12\x34\x56\x78padding"
        clip, = dc_sound_bank(data)
        self.assertEqual((clip["id"], clip["compressed_bytes"], clip["rate_flag"], clip["sample_rate"], clip["frames"]),
                         (0xEE9221ED, 4, 0xFFFF, 11025, 8))
        self.assertEqual(clip["adpcm"], b"\x12\x34\x56\x78")
        self.assertEqual(clip["sha256"], hashlib.sha256(clip["adpcm"]).hexdigest())

    def test_duplicate_ids_ranges_and_empty_banks_are_not_merged(self):
        data = struct.pack("<I", 2) + struct.pack("<IIHH", 7, 28, 2, 0) * 2 + b"abtrailing"
        clips = dc_sound_bank(data)
        self.assertEqual([c["slot"] for c in clips], [0, 1])
        self.assertEqual([c["adpcm"] for c in clips], [b"ab", b"ab"])
        self.assertTrue(all(c["sample_rate"] == 22050 for c in clips))
        self.assertEqual(dc_sound_bank(bytes(2048)), [])

    def test_bad_declared_table_and_sample_ranges(self):
        for data in (b"", b"abc", struct.pack("<I", 4097), struct.pack("<I", 2) + bytes(12),
                     struct.pack("<IIIHH", 1, 7, 0, 4, 0) + bytes(4),
                     struct.pack("<IIIHH", 1, 7, 16, 0, 0),
                     struct.pack("<IIIHH", 1, 7, 16, 5, 0) + bytes(4)):
            with self.subTest(data=data), self.assertRaises(ValueError):
                dc_sound_bank(data)

    def test_derived_ffmpeg_transport_does_not_alter_payload(self):
        for rate in (11025, 22050):
            raw = b"\xab\xcd\x01"
            transport = derived_str(raw, rate)
            self.assertEqual(struct.unpack_from("<7I", transport), (1, rate, 4, 3, 0, 6, 1))
            self.assertEqual(transport[213:224], b"Sega Stream")
            self.assertEqual(transport[2048:], raw)
        for raw, rate in ((b"", 22050), (bytes(65536), 11025), (b"a", 44100)):
            with self.assertRaises(ValueError):
                derived_str(raw, rate)

    def test_original_write_guard_and_worker_boundaries(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "original"
            for output in (source, source / "generated", ROOT / "game/generated"):
                with self.assertRaises(ValueError):
                    checked_output(source, output)
            for workers in (0, 9, True, 1.5):
                with self.assertRaises(ValueError):
                    export_dc_audio(source, Path(temporary) / "out", workers)


@unittest.skipUnless((SOURCE / "AZ00.DC").is_file(), "Requires extracted original Dreamcast files")
class OriginalDreamcastSoundTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.inventory = inventory_dc_audio(SOURCE)

    def test_all_banks_including_az00_have_exact_declared_ranges(self):
        value = self.inventory
        clips = [c for b in value["banks"] for c in b["clips"]]
        self.assertEqual((len(value["archives"]), len(value["banks"]), len(clips)), (140, 266, 1281))
        self.assertEqual(Counter(c["rate_flag"] for c in clips), {0: 1154, 1: 127})
        self.assertEqual((min(c["compressed_bytes"] for c in clips), max(c["compressed_bytes"] for c in clips)), (2220, 32768))
        self.assertEqual(sum(len(b["clips"]) for b in value["banks"] if b["archive"] == "AZ00.DC"), 112)
        self.assertGreater(len(clips), len({c["id"] for c in clips}))
        for bank in value["banks"]:
            original = (SOURCE / bank["archive"]).read_bytes()
            self.assertEqual(hashlib.sha256(original[bank["offset"]:bank["offset"] + bank["bytes"]]).hexdigest(), bank["sha256"])
            for clip in bank["clips"]:
                start = clip["archive_offset"]
                self.assertEqual(original[start:start + clip["compressed_bytes"]], clip["adpcm"])
        metadata = inventory_metadata(value)
        self.assertTrue(all("adpcm" not in c for b in metadata["banks"] for c in b["clips"]))

    @unittest.skipUnless(shutil.which("ffmpeg"), "Requires existing FFmpeg executable")
    def test_original_22k_and_11k_decode_exact_twice_compressed_frames(self):
        clips = [c for b in self.inventory["banks"] for c in b["clips"]]
        for rate in (22050, 11025):
            clip = next(c for c in clips if c["sample_rate"] == rate)
            wave, pcm = decode_aica(clip["adpcm"], rate)
            self.assertEqual((pcm["channels"], pcm["sample_rate"], pcm["bits"], pcm["frames"]), (1, rate, 16, clip["compressed_bytes"] * 2))
            self.assertEqual(decode_aica(clip["adpcm"], rate)[0], wave)
            print(f"Original Dreamcast rate{rate} ID{clip['id']:08x}: {clip['compressed_bytes']} ADPCM bytes -> {pcm['frames']} mono PCM16 frames; deterministic replay")

    @unittest.skipUnless(shutil.which("ffmpeg"), "Requires existing FFmpeg executable")
    def test_export_keeps_physical_bank_slot_paths_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as temporary:
            source, output = Path(temporary) / "source", Path(temporary) / "converted"
            source.mkdir()
            original = (SOURCE / "AA01.DC").read_bytes()
            (source / "AA01.DC").write_bytes(original)
            result = export_dc_audio(source, output, workers=2)
            self.assertEqual(result["counts"], {"archives": 1, "banks": 3, "empty_banks": 1, "clips": 7})
            self.assertEqual((source / "AA01.DC").read_bytes(), original)
            names = []
            for bank in result["banks"]:
                for clip in bank["clips"]:
                    start = clip["archive_offset"]
                    self.assertEqual((output / clip["adpcm_file"]).read_bytes(), original[start:start + clip["compressed_bytes"]])
                    self.assertEqual(clip["pcm"]["frames"], clip["compressed_bytes"] * 2)
                    names.append(clip["wave_file"])
            self.assertEqual(len(names), len(set(names)))
            with self.assertRaises(FileExistsError):
                export_dc_audio(source, output)


if __name__ == "__main__":
    unittest.main()
