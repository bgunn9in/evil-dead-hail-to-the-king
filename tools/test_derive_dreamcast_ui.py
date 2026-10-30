import copy
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import derive_dreamcast_ui as ui
import import_dreamcast_runtime as imp


def fixture():
    old=[dict(path=n,bytes=42,sha256='a'*64)for n in ('font.bmp','font.txt','scripts/levelA.jpc','gui/languages/en/inventory.bin')]
    base=dict(format=imp.FORMAT,status='verified-import',source_manifest_sha256=imp.SOURCE_SHA,
              inputs_manifest_sha256='b'*64,files=old,files_sha256=imp.digest(imp.json_bytes(old)))
    change=dict(path='font.bmp',bytes=43,sha256='c'*64,source_sha256='a'*64)
    overlay=dict(format=ui.FORMAT,files=[change])
    rows=[{k:change[k]for k in ('path','bytes','sha256')}]+old[1:]
    rows +=[dict(path=n,bytes=2,sha256='d'*64)for n in (ui.BASE,ui.OVERLAY)]
    manifest=dict(base,files=rows)
    return manifest,base,overlay


class UIRevisionTests(unittest.TestCase):
    def profile(self,root):
        base=root/'base';overlay=root/'overlay';base.mkdir();overlay.mkdir()
        (base/'provenance').mkdir();(base/'provenance/import-inputs.json').write_bytes(b'{}')
        (base/'font.bmp').write_bytes(b'old synthetic font');(overlay/'font.bmp').write_bytes(b'new synthetic font')
        (base/'font.txt').write_bytes(b'immutable synthetic font metrics')
        files=[dict(path=n,bytes=(base/n).stat().st_size,sha256=imp.digest((base/n).read_bytes()))
               for n in ('font.bmp','font.txt','provenance/import-inputs.json')]
        manifest=dict(format=imp.FORMAT,status='verified-import',source_manifest_sha256=imp.SOURCE_SHA,
                      inputs_manifest_sha256=imp.digest(b'{}'),files=files,file_count=len(files),
                      files_sha256=imp.digest(imp.json_bytes(files)))
        raw=imp.json_bytes(manifest);(base/imp.MARKER).write_bytes(raw)
        payload=(overlay/'font.bmp').read_bytes()
        revision=dict(format=ui.FORMAT,files=[dict(path='font.bmp',bytes=len(payload),
                      sha256=imp.digest(payload),source_sha256=files[0]['sha256'])])
        (overlay/'manifest.json').write_bytes(imp.json_bytes(revision))
        return base,overlay,imp.digest(raw)

    def test_only_explicit_ui_changes_with_exact_source_identity(self):
        m,b,o=fixture();self.assertEqual(set(ui.validate_tables(m,b,o)),{'font.bmp'})
        for changed in ('font.txt','scripts/levelA.jpc','gui/languages/en/inventory.bin','../font.bmp'):
            m,b,o=fixture();o['files'][0]['path']=changed
            with self.assertRaises(ValueError):ui.validate_tables(m,b,o)

    def test_reject_unlisted_change_duplicate_and_missing_provenance(self):
        for kind in ('unlisted','duplicate','missing','wrong-before','unknown-source'):
            m,b,o=fixture()
            if kind=='unlisted':m['files'][1]=dict(m['files'][1],sha256='e'*64)
            elif kind=='duplicate':o['files'].append(copy.deepcopy(o['files'][0]))
            elif kind=='missing':m['files'].pop()
            elif kind=='wrong-before':o['files'][0]['source_sha256']='f'*64
            else:b['source_manifest_sha256']='f'*64
            with self.assertRaises(ValueError):ui.validate_tables(m,b,o)

    def test_fresh_derive_roundtrip_and_actual_payload_verification(self):
        with tempfile.TemporaryDirectory()as temporary:
            root=Path(temporary);base,overlay,sha=self.profile(root);output=root/'output'
            result=ui.derive(base,overlay,output,sha);assets=output/'assets'
            self.assertEqual(result['file_count'],5)
            self.assertEqual(imp.verify(assets/'provenance/import-inputs.json',output)['rewritten_files'],0)
            self.assertEqual((base/'font.bmp').read_bytes(),b'old synthetic font')
            self.assertEqual((assets/'font.bmp').read_bytes(),b'new synthetic font')
            self.assertFalse((output/'INCOMPLETE').exists())
            with self.assertRaises(FileExistsError):ui.derive(base,overlay,output,sha)
            (assets/'font.bmp').unlink();(assets/'font.bmp').write_bytes(b'damaged')
            with self.assertRaises(ValueError):imp.verify(assets/'provenance/import-inputs.json',output)
            self.assertEqual((overlay/'font.bmp').read_bytes(),b'new synthetic font')
            (assets/'font.bmp').unlink();os.link(overlay/'font.bmp',assets/'font.bmp')
            (assets/'font.txt').unlink();(assets/'font.txt').write_bytes(b'damaged')
            with self.assertRaisesRegex(ValueError,'Imported asset bytes differ: font.txt'):
                imp.verify(assets/'provenance/import-inputs.json',output)
            self.assertEqual((base/'font.txt').read_bytes(),b'immutable synthetic font metrics')

    def test_incomplete_overlay_is_rejected_before_output_creation(self):
        with tempfile.TemporaryDirectory()as temporary:
            root=Path(temporary);base,overlay,sha=self.profile(root);output=root/'output'
            (overlay/'INCOMPLETE').write_text('Still producing\n',encoding='ascii')
            with self.assertRaises(ValueError):ui.derive(base,overlay,output,sha)
            self.assertFalse(output.exists())

    def test_provenance_reparse_is_rejected_before_reading(self):
        with tempfile.TemporaryDirectory()as temporary:
            root=Path(temporary);base,overlay,sha=self.profile(root);output=root/'output'
            result=ui.derive(base,overlay,output,sha);assets=output/'assets';link=assets/ui.BASE
            link.unlink()
            try:os.symlink(base/imp.MARKER,link)
            except OSError:self.skipTest('Symlink creation is unavailable')
            try:
                with patch.object(Path,'read_bytes',side_effect=AssertionError('Read before reparse check')):
                    with self.assertRaisesRegex(ValueError,'Symlink/reparse'):ui.verify_derivation(assets,result)
            finally:link.unlink()


if __name__=='__main__':unittest.main()
