from pathlib import Path
import tempfile
import unittest
from PIL import Image
from ed_movies import MOVIE_HEADER
from verify_movie_capture import verify


class MovieCaptureTests(unittest.TestCase):
    def test_original_pixels_stretch_vertically_and_preserve_bars(self):
        with tempfile.TemporaryDirectory() as directory:
            movie, capture = Path(directory) / "movie.edm", Path(directory) / "capture.bmp"
            movie.write_bytes(MOVIE_HEADER.pack(b"EDMOVIE1", 2, 1, 1, 15, 1, 8000, 1, 1, 2, 2, 6, 2)
                              + bytes((255, 0, 0, 0, 255, 0)) + b"\0\0")
            expected = Image.new("RGB", (6, 4))
            expected.paste(Image.frombytes("RGB", (2, 1), bytes((255, 0, 0, 0, 255, 0))).resize((4, 4), Image.Resampling.NEAREST), (1, 0))
            expected.save(capture)
            self.assertEqual(verify(capture, movie, 0)["viewport"], [1, 0, 4, 4])
            expected.putpixel((0, 0), (1, 0, 0)); expected.save(capture)
            with self.assertRaises(ValueError):
                verify(capture, movie, 0)
            with self.assertRaises(ValueError):
                verify(capture, movie, 1)
