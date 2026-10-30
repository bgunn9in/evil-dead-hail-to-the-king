from pathlib import Path
import hashlib
import json
import tempfile
import unittest
from export_dreamcast_scenes import loader_ash, checked_frame, load_contexts, scene_text, ROOT
from PIL import Image

SOURCE = ROOT / 'build/dreamcast-original-20261027/files'

class NativeSceneTests(unittest.TestCase):
    @unittest.skipUnless(SOURCE.is_dir(), 'Original extracted Dreamcast copy is unavailable')
    def test_actual_original_ash_matches_independent_previous_pose(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder);record = loader_ash(SOURCE, output)
            self.assertEqual(record['clip'], '01024084')
            self.assertEqual(record['vertex_count'], 1590)
            previous = ROOT / 'build/dreamcast-import-adaptation-private/visual-v2-ak01/ash.bin'
            self.assertEqual(hashlib.sha256((output/'ash.bin').read_bytes()).hexdigest(),
                             '39ae3c38ad88aae52434755c0d66c6b1f46058c2f0219b706b7ebe913bf108b1')
            if previous.is_file():
                self.assertEqual((output/'ash.bin').read_bytes(), previous.read_bytes())

    def test_frame_hash_size_mode_and_name_are_checked_before_copy(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder);(root/'native-rgb').mkdir()
            path = root/'native-rgb/frame_000000.bmp';image = Image.new('RGB', (320,240), (16,32,64));image.save(path)
            record = {'avi_index': 0, 'native_bmp': path.name, 'bmp_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                      'rgb_sha256': hashlib.sha256(image.tobytes()).hexdigest()}
            self.assertEqual(checked_frame(root, record, 0), path)
            for key, value in (('avi_index', 1), ('native_bmp', '../escape.bmp'), ('bmp_sha256', '0'*64), ('rgb_sha256','0'*64)):
                changed = dict(record);changed[key] = value
                with self.assertRaises(ValueError):checked_frame(root, changed, 0)

    @unittest.skipUnless(SOURCE.is_dir(), 'Original extracted Dreamcast copy is unavailable')
    def test_native_scene_factory_and_loader_use_actual_source_geometry(self):
        _, contexts, switches, factories = load_contexts(SOURCE)
        self.assertEqual([len(factories[x]) for x in 'AB'], [33,30])
        self.assertIn('bl01', contexts);self.assertIn('bl02', contexts)
        self.assertNotIn('bg01', contexts);self.assertNotIn('bm21', contexts)
        source = contexts['ak01']['scene']
        text = scene_text(source, source['markers'][4], 1590)
        self.assertTrue(text.startswith('EDSCENE2\n1590 15\n'))
        self.assertTrue(any(s['destination']==0x616b0100 for s in switches))

if __name__ == '__main__':
    unittest.main()
