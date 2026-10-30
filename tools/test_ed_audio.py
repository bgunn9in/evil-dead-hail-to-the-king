from pathlib import Path
import struct
import tempfile
import unittest

from ed_audio import ROOT, export_runtime_audio, pcm_wave, read_audio_assets, read_stream_assets, sound_bundle, stream_id, stream_name


def wave(pcm=b"\0\0" * 8):
    fmt = struct.pack("<HHIIHH", 1, 1, 22050, 44100, 2, 16)
    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(pcm)) + pcm
    return b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks


class SoundBundleTests(unittest.TestCase):
    def test_original_stream_identity(self):
        self.assertEqual(stream_name(0x660200FF), "actf2")
        self.assertEqual(stream_id("actf17"), 0x66110000)
        for name in ("../actf2", "actf256", "actf02", "act2"):
            with self.assertRaises(ValueError):
                stream_id(name)

    def test_full_unsigned_ids_and_riff_size_override_low16_table_size(self):
        original = wave(b"\0\0" * 40000)
        bundle = struct.pack("<4I", 1, 0xEE9221ED, 16, len(original) & 65535) + original
        clip, = sound_bundle(bundle)
        self.assertEqual(clip["id"], 0xEE9221ED)
        self.assertEqual(clip["wave"], original)
        self.assertEqual(clip["frames"], 40000)
        self.assertNotIn("unsupported", clip)

    def test_truncated_wave_never_consumes_next_sound_and_bad_table_rejected(self):
        original = wave()
        broken = bytearray(original)
        struct.pack_into("<I", broken, 4, len(original) + 65528)
        bundle = struct.pack("<I6I", 2, 3, 28, len(broken), 4, 28 + len(broken), len(original))
        clips = list(sound_bundle(bundle + broken + original))
        self.assertEqual(clips[0]["wave"], broken)
        self.assertIn("unsupported", clips[0])
        self.assertEqual(clips[1]["wave"], original)
        invalid = struct.pack("<4I", 1, 1, 0, 1) + original
        with self.assertRaises(ValueError):
            list(sound_bundle(invalid))
        with self.assertRaises(ValueError):
            pcm_wave(original[:-2])
        unaligned = wave(b"\0" * 3 + b"\0")
        unaligned = bytearray(unaligned)
        struct.pack_into("<I", unaligned, 28, 1234)
        with self.assertRaises(ValueError):
            pcm_wave(unaligned)


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class OriginalAudioTests(unittest.TestCase):
    def test_source_packages_and_scene_overrides_preserve_full_ids(self):
        assets = read_audio_assets(ROOT / "game")
        clips = {(clip["bucket"], clip["id"]): clip for clip in assets["clips"]}
        self.assertEqual((len(clips), len(assets["unsupported"])), (235, 3))
        self.assertEqual(sum(c["parent_type"]==13 for c in clips.values()),96)
        self.assertEqual(sum(c["parent_type"]==14 for c in clips.values()),139)
        self.assertIn(("global",0x0D9A4F40),clips)
        self.assertIn(("ah01", 0x2E8720CD), clips)
        self.assertEqual(clips["ak01", 0x0E8A4ACA]["wave"], clips["ak09", 0x0E8A4ACA]["wave"])
        self.assertNotEqual(clips["global", 0xEE9221ED]["wave"], clips["global_b", 0xEE9221ED]["wave"])
        self.assertTrue(all(clip["source"] == "bz01" for clip in assets["unsupported"]))
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)
            result = export_runtime_audio(ROOT / "game", target)
            self.assertEqual(result["clips"], 235)
            self.assertEqual(result["streams"], 52)
            lines = (target / "audio/manifest.txt").read_text("ascii").splitlines()
            self.assertEqual(lines[:2], ["EDAUDIO1", "235"])
            self.assertIn(f"ah01 2e8720cd {len(clips['ah01', 0x2E8720CD]['wave'])}", lines)
            for clip in assets["unsupported"]:
                self.assertEqual((target / f"audio/unsupported/bz01/{clip['id']:08x}.wav").read_bytes(), clip["wave"])
            self.assertEqual((target / "audio/streams/66020000.wav").read_bytes(), (ROOT / "game/data/actf2.wav").read_bytes())
            streams = read_stream_assets(ROOT / "game")
            self.assertEqual(sum(c["bytes"] for c in streams), 180490724)
            self.assertEqual((target / "audio/streams/manifest.txt").read_text().splitlines()[:2], ["EDSTREAM1", "52"])
        with self.assertRaises(ValueError):
            export_runtime_audio(ROOT / "game", ROOT / "game/generated")

    def test_actual_tree_animation_sound_bank(self):
        assets=read_audio_assets(ROOT/"game",["at02"])
        clips={clip["id"]:clip for clip in assets["clips"]if clip["bucket"]=="at02" and clip["parent_type"]==13}
        self.assertEqual(set(clips),{0x0D9A4F40,0x00800001,0x0DCE2084,0x0DCE2280,0x0DCE2FC4,0x0DCE34C4,0x0DCE3400})
        self.assertTrue(all(pcm_wave(clip["wave"])["duration"]>0 for clip in clips.values()))


if __name__ == "__main__":
    unittest.main()
