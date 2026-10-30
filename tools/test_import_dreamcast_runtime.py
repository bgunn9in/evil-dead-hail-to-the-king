import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import import_dreamcast_runtime as importer


class ImportGuardTests(unittest.TestCase):
    def test_original_game_and_raw_source_verification_are_refused_without_writes(self):
        with tempfile.TemporaryDirectory() as temporary:
            inputs=Path(temporary)/'inputs.json';inputs.write_text('{}')
            blocked=importer.ROOT/'game'/'__must_never_be_created__'
            self.assertFalse(blocked.exists())
            with self.assertRaisesRegex(ValueError,'outside original'):
                importer.output_guard(blocked,[])
            with patch.object(importer,'load') as load:
                with self.assertRaisesRegex(ValueError,'outside original'):
                    importer.verify(inputs,importer.ROOT/'game')
                load.assert_not_called()
            raw=Path(temporary)/'raw';raw.mkdir();inputs.write_text(json.dumps(dict(format='ED-DREAMCAST-RUNTIME-INPUTS1',original_files=str(raw))))
            with self.assertRaisesRegex(ValueError,'outside original'):importer.verify(inputs,raw)
            self.assertFalse(blocked.exists())

    def test_relative_paths_cannot_escape_or_create_windows_streams(self):
        self.assertEqual(importer.relative('levelB/gui/text.bin'),'levelB/gui/text.bin')
        for name in ('','../outside','/outside','a/../b','a//b','a\\b','C:/outside','a:b','./a'):
            with self.assertRaises(ValueError):importer.relative(name)

    def test_fresh_output_guard_preserves_existing_and_rejects_input_children(self):
        with tempfile.TemporaryDirectory() as temporary:
            source=Path(temporary)/'source';source.mkdir();existing=Path(temporary)/'existing';existing.mkdir()
            with self.assertRaises(FileExistsError):importer.output_guard(existing,[source])
            with self.assertRaisesRegex(ValueError,'overlaps'):importer.output_guard(source/'nested',[source])
            self.assertTrue(source.is_dir() and existing.is_dir())

    def test_actual_symlink_is_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);source=root/'original';source.write_bytes(b'immutable');link=root/'alias'
            try:os.symlink(source,link)
            except OSError:self.skipTest('Symlink creation is unavailable')
            try:
                with self.assertRaisesRegex(ValueError,'Symlink/reparse'):importer.checked_path(link)
                self.assertEqual(source.read_bytes(),b'immutable')
            finally:link.unlink()

    def test_duplicate_destinations_and_raw_media_are_not_silently_imported(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);source=root/'native.bin';source.write_bytes(b'native');p=importer.Plan();p.add(source,'actors/native.bin')
            with self.assertRaisesRegex(ValueError,'Duplicate'):p.add(source,'actors/native.bin')
            legacy=root/'bad.pc';legacy.write_bytes(b'not an imported component')
            with self.assertRaisesRegex(ValueError,'Raw executable/media'):p.add(legacy,'bad.pc')
            p.add(source,'actors/native.bin',overlay=True)
            self.assertEqual(source.read_bytes(),b'native')

    def fixture(self,root,wrong=False):
        inputs=root/'inputs.json';inputs.write_text('{}');a=root/'copy.bin';a.write_bytes(b'copy');b=root/'linked.bin';b.write_bytes(b'generated immutable media')
        plan=importer.Plan();plan.add(a,'data/copy.bin',expected='wrong' if wrong else None);plan.add(b,'data/linked.bin',linked=True)
        return inputs,plan,{'world.txt':b'EDWORLD1\ntest\n0\n'},[inputs,a,b],dict(scope='Synthetic file-seal test only')

    def test_copy_seal_and_independent_verify_detect_modified_or_extra_assets(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);inputs,p,g,s,m=self.fixture(root);output=root/'output'
            with patch.object(importer,'plan_inputs',return_value=(p,g,s,m)):
                result=importer.compose(inputs,output)
            self.assertEqual(result['status'],'verified-import');self.assertEqual(result['file_count'],3)
            self.assertFalse((output/'INCOMPLETE').exists())
            self.assertEqual(importer.verify(inputs,output)['rewritten_files'],0)
            (output/'assets/extra.bin').write_bytes(b'extra')
            with self.assertRaisesRegex(ValueError,'file set differs'):importer.verify(inputs,output)
            (output/'assets/extra.bin').unlink();(output/'assets/data/copy.bin').write_bytes(b'damaged')
            with self.assertRaisesRegex(ValueError,'bytes differ'):importer.verify(inputs,output)
            self.assertEqual((root/'copy.bin').read_bytes(),b'copy')

    def test_failed_hash_seal_keeps_incomplete_and_never_writes_verified_marker(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);inputs,p,g,s,m=self.fixture(root,True);output=root/'failed'
            with patch.object(importer,'plan_inputs',return_value=(p,g,s,m)):
                with self.assertRaisesRegex(ValueError,'output hash differs'):importer.compose(inputs,output)
            self.assertTrue((output/'INCOMPLETE').exists());self.assertFalse((output/'assets/dreamcast-runtime.json').exists())
            self.assertEqual((root/'copy.bin').read_bytes(),b'copy')
            with self.assertRaisesRegex(ValueError,'incomplete'):importer.verify(inputs,output)

    def test_sealed_profile_verification_does_not_require_originals_to_still_exist(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);inputs,p,g,s,m=self.fixture(root);output=root/'output'
            inputs.write_text(json.dumps(dict(format='ED-DREAMCAST-RUNTIME-INPUTS1',original_files=str(root/'archived-originals'))))
            with patch.object(importer,'plan_inputs',return_value=(p,g,s,m)):importer.compose(inputs,output)
            self.assertEqual(importer.verify(inputs,output)['rewritten_files'],0)

    def test_unrecognized_input_schema_is_rejected_before_output_creation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);inputs=root/'inputs.json';inputs.write_text('{"format":"anything"}');output=root/'new'
            with self.assertRaisesRegex(ValueError,'input schema'):importer.compose(inputs,output)
            self.assertFalse(output.exists())


if __name__=='__main__':unittest.main()
