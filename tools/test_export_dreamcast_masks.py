import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
from export_dreamcast_masks import decode_dc_masks, render_table, write_layers, export_sources

class ExportTests(unittest.TestCase):
    def test_four_alpha_and_fallback_keep_raw_bits(self):
        packed = struct.pack('<IIfI', 1, 0x01010000, 0, 16) + b'\x1b'
        layers = decode_dc_masks(packed)
        self.assertEqual(layers[0]['distance_bits'], 0)
        self.assertEqual(layers[0]['effective_distance'], 1)
        self.assertEqual(render_table(layers), 'EDDCMASKDRAW1\n1\n0.0 0.0 10.0 1.875 1.0 1.0 0.875 0.015625 0.00390625\n')
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            write_layers(layers, out, '2_31')
            plane = (out / 'mask_2_31_0.coverage8').read_bytes()
            self.assertEqual(len(plane), 65536)
            self.assertEqual(plane[:4], bytes([0, 85, 170, 255]))
            with self.assertRaises(FileExistsError):
                write_layers(layers, out, '2_31')
            with self.assertRaises(ValueError):
                write_layers(layers, out, '../escape')

    def test_source_output_guard(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'original'
            source.mkdir()
            (source / 'sentinel').write_bytes(b'original')
            for out in (source, source / 'child', source.parent):
                with self.assertRaises(ValueError):
                    export_sources(source, out)
            self.assertEqual((source / 'sentinel').read_bytes(), b'original')
            with self.assertRaises(ValueError):
                export_sources(source, Path(folder) / 'no-archives')

if __name__ == '__main__':
    unittest.main()
