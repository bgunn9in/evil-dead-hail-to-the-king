import struct
import unittest
from export_language_sidecars import english_inventory
from derive_dreamcast_language import inventory_shape


class LanguageSidecarTests(unittest.TestCase):
    def test_only_labels_change_and_malformed_input_rejected(self):
        item = dict(item_id=46, flags=60, texture_resource_id=123, shape_index=0,
                    uv_rect=[0., 0., 1., 1.], aspect=1., name_raw='Health kit', description_raw='Original medicine.')
        prefix = struct.pack('<4I5f', 46, 60, 0, 0, 0., 0., 1., 1., 1.)
        header = struct.pack('<8s6I', b'EDINV2\0\0', 1251, 1, 1, 123, 64, 64)
        original = header + prefix + struct.pack('<2I', 2, 3) + b'RUOLD'
        result = english_inventory(original, {'items': [item]})
        self.assertEqual(result[:len(header) + 36], original[:len(header) + 36])
        self.assertTrue(result.endswith(b'Health kitOriginal medicine.'))
        for malformed in (original[:-1], original + b'x', b'EDINV1\0\0' + original[8:], original[:12] + struct.pack('<I', 9) + original[16:]):
            with self.assertRaises(ValueError):
                english_inventory(malformed, {'items': [item]})

    def test_profile_inventory_requires_all121_ids_and_unchanged_geometry(self):
        header=struct.pack('<8s6I',b'EDINV2\0\0',1251,1,121,123,512,1024)
        records=b''.join(struct.pack('<4I5f2I',n,60,0,0,0.,0.,1.,1.,1.,1,1)+b'AB' for n in range(121))
        raw=header+records;shape=inventory_shape(raw,True)
        self.assertEqual(shape[:len(header)],header)
        for malformed in (raw[:-1],raw+b'x',raw[:16]+struct.pack('<I',120)+raw[20:],header+records[:46]+records):
            with self.assertRaises(ValueError):inventory_shape(malformed,True)


if __name__ == '__main__':
    unittest.main()
