import copy
import json
from pathlib import Path
import struct
import tempfile
import unittest

from ed_dc_media import export_dc_media, sfd_metadata, str_header
from ed_movies import ROOT


class DreamcastMediaTests(unittest.TestCase):
    def test_stream_declared_end_excludes_packet_padding(self):
        data = bytearray(2048 + 32768)
        struct.pack_into("<8I", data, 0, 1, 44100, 4, 16384, 1, 30000, 2, 99)
        data[213:224] = b"Sega Stream"
        metadata = str_header(data)
        self.assertEqual((metadata["frames"], metadata["physical_frames"], metadata["padding_frames"]), (30000, 32768, 2768))
        self.assertEqual(metadata["source_header_words"][-1], 99)
        for index, value in ((0, 2), (1, 22050), (2, 16), (3, 8192), (5, 0), (5, 32769), (6, 1)):
            bad = bytearray(data)
            struct.pack_into("<I", bad, index * 4, value)
            with self.subTest(index=index, value=value), self.assertRaises(ValueError):
                str_header(bad)
        for bad in (data[:-1], data[:2048], b"", bytes(len(data))):
            with self.assertRaises(ValueError):
                str_header(bad)

    def test_sfd_uses_decoded_frame_count_and_average_not_double_field_rate(self):
        source = {"streams": [
            {"codec_type": "video", "codec_name": "mpeg1video", "width": 320, "height": 240,
             "avg_frame_rate": "30/1", "r_frame_rate": "60/1", "nb_read_frames": "300", "duration": "10", "start_time": "0"},
            {"codec_type": "audio", "codec_name": "adpcm_adx", "sample_rate": "44100", "channels": 2,
             "duration": "10.01", "start_time": "0"}]}
        metadata = sfd_metadata(source)
        self.assertEqual((metadata["frames"], metadata["fps_numerator"], metadata["duration"]), (300, 30, 10))
        for index, key, value in ((0, "nb_read_frames", "0"), (0, "avg_frame_rate", "0/0"),
                                  (0, "avg_frame_rate", "60/1"), (0, "width", 640),
                                  (1, "codec_name", "binkaudio_rdft"), (1, "start_time", "0.1"),
                                  (1, "duration", "9.5")):
            bad = copy.deepcopy(source)
            bad["streams"][index][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                sfd_metadata(bad)
        with self.assertRaises(ValueError):
            sfd_metadata({"streams": []})

    def test_output_guards_before_conversion(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source"
            source.mkdir()
            for output in (source, source / "generated", ROOT / "game/generated"):
                with self.assertRaises(ValueError):
                    export_dc_media(source, output)
            with self.assertRaises(FileExistsError):
                export_dc_media(source, Path(directory))
            for workers in (0, 9, True, 1.5):
                with self.assertRaises(ValueError):
                    export_dc_media(source, Path(directory) / "out", workers)

    @unittest.skipUnless((ROOT / "build/dreamcast-original-20261027/files/ACTA0.STR").is_file(), "Requires original Dreamcast files")
    def test_all_original_stream_headers_and_sfd_probes(self):
        source = ROOT / "build/dreamcast-original-20261027/files"
        streams = sorted(source.glob("*.STR"))
        self.assertEqual(len(streams), 52)
        for path in streams:
            self.assertGreater(str_header(path.read_bytes())["padding_frames"], 0)
        probe = source.parent / "sfd-probes.json"
        if probe.exists():
            values = json.loads(probe.read_text("utf-8"))
            self.assertEqual(len(values), 56)
            for value in values:
                sfd_metadata(value)


if __name__ == "__main__":
    unittest.main()
