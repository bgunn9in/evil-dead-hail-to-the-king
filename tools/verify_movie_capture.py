"""Compare a GPU movie capture with the exact exported original RGB frame."""
import argparse
from pathlib import Path

from PIL import Image, ImageChops

from ed_movies import MOVIE_HEADER


def verify(capture, movie, frame):
    with Path(movie).open("rb") as stream:
        header = stream.read(MOVIE_HEADER.size)
        if len(header) != MOVIE_HEADER.size:
            raise ValueError("Truncated movie header")
        magic, width, height, count, _, _, _, _, _, display_width, display_height, video_bytes, _ = MOVIE_HEADER.unpack(header)
        if (magic != b"EDMOVIE1" or not 0 < width <= 2048 or not 0 < height <= 2048
                or not 0 <= frame < count or video_bytes != width * height * count * 3
                or display_width != width or display_height not in (height, height * 2)):
            raise ValueError("Invalid movie frame or dimensions")
        stream.seek(MOVIE_HEADER.size + width * height * frame * 3)
        pixels = stream.read(width * height * 3)
        if len(pixels) != width * height * 3:
            raise ValueError("Truncated movie frame")
    with Image.open(capture) as image:
        actual = image.convert("RGB")
    w, h = actual.size
    if w * display_height > h * display_width:
        view_w, view_h = h * display_width // display_height, h
    else:
        view_w, view_h = w, w * display_height // display_width
    x, y = (w - view_w) // 2, (h - view_h) // 2
    expected = Image.new("RGB", (w, h))
    expected.paste(Image.frombytes("RGB", (width, height), pixels).resize((view_w, view_h), Image.Resampling.NEAREST), (x, y))
    if ImageChops.difference(actual, expected).getbbox():
        raise ValueError("GPU capture differs from the original frame or its black bars")
    return {"capture_size": [w, h], "source_frame": frame, "viewport": [x, y, view_w, view_h], "pixel_error": 0}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("movie", type=Path)
    parser.add_argument("frame", type=int)
    args = parser.parse_args()
    try:
        print(verify(args.capture, args.movie, args.frame))
    except (OSError, ValueError) as error:
        parser.exit(1, f"Error: {error}\n")
