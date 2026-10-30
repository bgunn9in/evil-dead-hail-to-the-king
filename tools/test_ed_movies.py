import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile
import unittest
from unittest.mock import patch

import ed_movies
from ed_movies import ROOT, MOVIE_HEADER, bink_header, export_runtime_movies, movie_id, movie_name, sample_movie


class MovieFormatTests(unittest.TestCase):
    def test_original_id_mapping_and_header_ranges(self):
        for name, identity in (("ax01", 0x61303100), ("ax01b", 0x61303162), ("ax06", 0x61303600), ("ed2ab", 0xBEAD0002)):
            self.assertEqual(movie_name(identity), name)
            self.assertEqual(movie_id(name), identity)
        with self.assertRaises(ValueError):
            movie_name(0)
        data = bytearray(100)
        struct.pack_into("<4s10I", data, 0, b"BIKi", 92, 1, 36, 1, 640, 240, 15, 1, 0x10000000, 1)
        struct.pack_into("<IHHII", data, 44, 4096, 44100, 0xE000, 0, 65)
        self.assertEqual((bink_header(data)["display_width"], bink_header(data)["display_height"]), (640, 480))
        struct.pack_into("<I", data, 56, 1000)
        with self.assertRaises(ValueError):
            bink_header(data)


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class OriginalMovieTests(unittest.TestCase):
    def test_all_source_basenames_and_presentation(self):
        paths = list((ROOT / "game/fmv").glob("*.bik"))
        self.assertEqual(len(paths), 54)
        for path in paths:
            metadata = bink_header(path.read_bytes())
            self.assertEqual(movie_name(movie_id(path.stem)), path.stem)
            self.assertEqual((metadata["width"], metadata["height"], metadata["display_height"]), (640, 240, 480))

    def test_actual_decode_samples_cache_and_failed_redecode_preserve_old_movie(self):
        source = ROOT / "game/fmv/ax06.bik"
        source_hash = ed_movies.file_hash(source)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            metadata, = export_runtime_movies(ROOT / "game", root, [0x61303600])
            target = root / "movies/61303600.edm"
            before = ed_movies.file_hash(target)
            with target.open("rb") as stream:
                header = MOVIE_HEADER.unpack(stream.read(64))
                self.assertEqual(header[:6], (b"EDMOVIE1", 640, 240, 66, 15, 1))
                self.assertEqual(header[9:11], (640, 480))
                self.assertEqual(header[11], 640 * 240 * 3 * 66)
                for index, rgb in sample_movie(source):
                    stream.seek(64 + index * 640 * 240 * 3)
                    self.assertEqual(stream.read(len(rgb)), rgb)
            stamp = target.stat().st_mtime_ns
            export_runtime_movies(ROOT / "game", root, [0x61303600])
            self.assertEqual(target.stat().st_mtime_ns, stamp)
            sidecar = root / "movies/61303600.json"
            stale = json.loads(sidecar.read_text("utf-8")); stale["converter"]["ffmpeg_version"] = "stale"
            sidecar.write_text(json.dumps(stale), "utf-8")
            real = ed_movies.command

            def failed(arguments, **options):
                if "rawvideo" in arguments:
                    raise ValueError("intentional decoder failure")
                return real(arguments, **options)

            with patch.object(ed_movies, "command", failed), self.assertRaises(ValueError):
                export_runtime_movies(ROOT / "game", root, [0x61303600])
            self.assertEqual(ed_movies.file_hash(target), before)
            self.assertFalse(list((root / "movies").glob(".movie-*")))
            with patch.object(ed_movies, "atomic_json", side_effect=OSError("intentional metadata write failure")), self.assertRaises(OSError):
                export_runtime_movies(ROOT / "game", root, [0x61303600])
            self.assertEqual(ed_movies.file_hash(target), before)
        self.assertEqual(ed_movies.file_hash(source), source_hash)
        with self.assertRaises(ValueError):
            export_runtime_movies(ROOT / "game", ROOT / "game/generated")


if __name__ == "__main__":
    unittest.main()
