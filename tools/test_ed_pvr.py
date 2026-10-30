import struct
import unittest

from ed_pvr import decode_pvr


def texture(pixel, values):
    payload = struct.pack('<4H', *values)
    return struct.pack('<4sIBBHHH', b'PVRT', 8 + len(payload), pixel, 1, 0, 2, 2) + payload


class PvrTests(unittest.TestCase):
    def test_argb4444_alpha_and_y_first_twiddle(self):
        image = decode_pvr(texture(2, [0xff00, 0xf0f0, 0xf00f, 0x8123]))
        self.assertEqual([image.getpixel((x, y)) for y in range(2) for x in range(2)], [(255, 0, 0, 255), (0, 0, 255, 255),
                                               (0, 255, 0, 255), (17, 34, 51, 136)])

    def test_argb1555_transparency(self):
        image = decode_pvr(texture(0, [0xfc00, 0x83e0, 0x801f, 0x7fff]))
        self.assertEqual(image.getpixel((0, 0)), (255, 0, 0, 255))
        self.assertEqual(image.getpixel((1, 1)), (255, 255, 255, 0))

    def test_reject_truncated_invalid_and_unsupported(self):
        good = texture(2, [0, 0, 0, 0])
        for data in (b'', good[:-1], b'FAIL' + good[4:], texture(4, [0, 0, 0, 0])):
            with self.assertRaises(ValueError):
                decode_pvr(data)

    def test_rectangular_twiddle_blocks_both_directions(self):
        def make(width, height):
            # Two 2x2 squares, each with four distinct ARGB4444 red levels.
            payload = struct.pack('<8H', *(0xf000 | (n << 8) for n in range(8)))
            return struct.pack('<4sIBBHHH', b'PVRT', 8 + len(payload), 2, 13, 0, width, height) + payload
        image = decode_pvr(make(2, 4))
        self.assertEqual([image.getpixel((x, y))[0] for y in range(4) for x in range(2)],
                         [0, 34, 17, 51, 68, 102, 85, 119])
        image = decode_pvr(make(4, 2))
        self.assertEqual([image.getpixel((x, y))[0] for y in range(2) for x in range(4)],
                         [0, 34, 68, 102, 17, 51, 85, 119])

    def test_yuv_linear_and_twiddled_pair_order_and_signed_division(self):
        # U,Y0,V,Y1 on each line; second line has different chroma.
        linear = [0x647f, 0xc881, 0x3281, 0x9680]
        def make(layout, values):
            return struct.pack('<4sIBBHHH4H', b'PVRT', 16, 3, layout, 0, 2, 2, *values)
        expected = [(101, 100, 99, 255), (201, 200, 199, 255),
                    (50, 50, 51, 255), (150, 150, 151, 255)]
        for layout, values in ((9, linear), (1, [linear[0], linear[2], linear[1], linear[3]])):
            image = decode_pvr(make(layout, values))
            self.assertEqual([image.getpixel((x, y)) for y in range(2) for x in range(2)], expected)
        clipped = decode_pvr(make(9, [0x00ff, 0x0000, 0xffff, 0xff00]))
        self.assertEqual(clipped.getpixel((0, 0)), (0, 44, 218, 255))

    def test_dimensions_formats_and_extra_bytes_remain_strict(self):
        good = bytearray(texture(2, [0, 0, 0, 0]))
        for offset, value in ((9, 2), (10, 1), (12, 3), (14, 4)):
            bad = bytearray(good); bad[offset] = value
            with self.assertRaises(ValueError):
                decode_pvr(bad)
        with self.assertRaises(ValueError):
            decode_pvr(good + b'\0')


if __name__ == '__main__':
    unittest.main()
