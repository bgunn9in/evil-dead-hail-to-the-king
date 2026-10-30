import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

from PIL import Image
import import_dreamcast_runtime as imp
import upscale_dreamcast_runtime as up


class UpscaleTests(unittest.TestCase):
    def test_bmp_keeps_alpha_instead_of_pillow_opaque_bi_rgb(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'font.bmp'
            im = Image.new('RGBA', (2, 3), (17, 17, 17, 17)); im.save(path)
            self.assertEqual(up.bmp(path).tobytes(), im.tobytes())
            path.write_bytes(path.read_bytes()[:-1])
            with self.assertRaises(ValueError): up.bmp(path)

    def test_prepare_preserves_native_font_title_tabs_and_vfx_but_upscales_scene(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); source = root / 'native' / 'assets'; source.mkdir(parents=True)
            preserved = ('font.bmp', 'font.txt', 'gui/textures/0b942034.rgba',
                         'gui/textures/0b942054.rgba', 'aa01/main-menu.texture', 'vfx/atlas.rgba',
                         'levelB/font.bmp', 'levelB/font.txt', 'levelB/gui/textures/0b942034.rgba',
                         'levelB/gui/textures/0b942054.rgba', 'levelB/bb01/main-menu.texture', 'levelB/vfx/atlas.rgba')
            for name in preserved:
                path = source / name; path.parent.mkdir(parents=True, exist_ok=True)
                if path.suffix == '.bmp':
                    font = Image.new('RGBA', (256, 512))
                    font.paste((255,) * 4, (17, 0, 18, 24)); font.save(path)
                elif path.suffix == '.txt':
                    path.write_bytes(b'EDFONT2\n18 24 14 256 512\nCP1251\nAB\n')
                else:
                    path.write_bytes(struct.pack('<8sIII', b'EDTEX1\0\0', 7, 2, 1) +
                                     bytes((255, 255, 255, 255, 17, 31, 63, 0)))
            (source / 'ak01').mkdir()
            photo = Image.new('RGB', (320, 240), (17, 31, 63)); photo.save(source / 'ak01/background_0.bmp')
            (source / 'ak01/background_size.txt').write_bytes(b'EDPROFILEBG1\n320 240 0.875\n')
            (source / 'models/textures').mkdir(parents=True)
            model = source / 'models/textures/01020304.rgba'
            model.write_bytes(struct.pack('<8sIII', b'EDTEX1\0\0', 0x01020304, 3, 2) +
                              Image.new('RGBA', (3, 2), (17, 31, 63, 255)).tobytes())
            (source / 'inventory.bin').write_bytes(
                struct.pack('<8s6I', b'EDINV2\0\0', 1251, 1, 121, 1, 256, 512) + b'unchanged labels')
            (source / 'inventory_00000001.rgba').write_bytes(Image.new('RGBA', (256, 512), (31,) * 4).tobytes())
            (source / 'provenance').mkdir()
            (source / 'provenance/import-inputs.json').write_bytes(b'{}\n')
            originals = {p.relative_to(source).as_posix(): p.read_bytes() for p in source.rglob('*') if p.is_file()}
            records = [dict(path=n, bytes=len(data), sha256=imp.digest(data)) for n, data in sorted(originals.items())]
            (source / imp.MARKER).write_bytes(imp.json_bytes(dict(files=records, source_manifest_sha256=imp.SOURCE_SHA)))
            executable = root / 'waifu' / 'waifu.exe'; executable.parent.mkdir(); executable.write_bytes(b'fake tool')
            models = executable.parent / 'models-upconv_7_photo'; models.mkdir()
            for name in ('scale2.0x_model.bin', 'scale2.0x_model.param'):
                (models / name).write_bytes(b'fake model')

            def fake_neural(command, **kwargs):
                self.assertEqual(command[command.index('-n') + 1], '-1')
                self.assertEqual(command[command.index('-s') + 1], '2')
                destination = Path(command[command.index('-o') + 1])
                for path in Path(command[command.index('-i') + 1]).glob('*.png'):
                    with Image.open(path) as image:
                        image.resize((image.width * 2, image.height * 2), Image.Resampling.NEAREST).save(destination / path.name)

            output = root / 'derived'
            with mock.patch.object(imp, 'verify') as verify, mock.patch.object(up.subprocess, 'run', side_effect=fake_neural) as neural:
                receipt = up.prepare(root / 'inputs.json', source, output, executable)
            verify.assert_called_once_with(root / 'inputs.json', source.parent)
            neural.assert_called_once()
            changed = {row['path'] for row in receipt['changed_files']}
            for name in preserved:
                self.assertEqual((output / 'assets' / name).read_bytes(), originals[name], name)
                self.assertNotIn(name, changed)
            self.assertEqual(up.bmp(output / 'assets/ak01/background_0.bmp').size, (640, 480))
            _, texture = up.rgba_texture((output / 'assets/models/textures/01020304.rgba').read_bytes())
            self.assertEqual(texture.size, (6, 4))
            self.assertEqual((output / 'assets/inventory_00000001.rgba').stat().st_size, 512 * 1024 * 4)
            self.assertEqual(struct.unpack_from('<III', (output / 'assets/inventory.bin').read_bytes(), 20), (1, 512, 1024))
            self.assertEqual((output / 'assets/ak01/background_size.txt').read_bytes(), b'EDPROFILEBG1\n640 480 0.875\n')
            self.assertEqual(changed, {'ak01/background_0.bmp', 'ak01/background_size.txt', 'models/textures/01020304.rgba',
                                       'inventory.bin', 'inventory_00000001.rgba'})
            self.assertTrue((output / 'INCOMPLETE').is_file())
            for name, raw in originals.items():
                self.assertEqual((source / name).read_bytes(), raw, name)

    def test_historical_filtered_font_title_tabs_and_vfx_seals_still_verify(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); (root / 'provenance').mkdir()
            before = [dict(path=name, bytes=1, sha256='a' * 64) for name in
                      ('font.bmp', 'font.txt', 'gui/textures/0b942034.rgba', 'gui/textures/0b942054.rgba',
                       'aa01/main-menu.texture', 'vfx/atlas.rgba')]
            base = dict(format=imp.FORMAT, status='verified-import', source_manifest_sha256=imp.SOURCE_SHA,
                        inputs_manifest_sha256='inputs', files=before, files_sha256=imp.digest(imp.json_bytes(before)))
            raw = imp.json_bytes(base); (root / up.BASE_RECORD).write_bytes(raw)
            changes = [dict(path=r['path'], source_sha256=r['sha256'], sha256='b' * 64) for r in before]
            manifest = dict(inputs_manifest_sha256='inputs', files=[dict(r, sha256='b' * 64) for r in before] + [dict(path=up.BASE_RECORD)],
                            visual_upscale=dict(format=up.FORMAT, scale=2, base_manifest_path=up.BASE_RECORD,
                                                base_manifest_sha256=imp.digest(raw), changed_files=changes))
            up.verify_derivation(root, manifest)

    def test_inventory_changes_only_dimensions_not_labels_or_uv(self):
        raw = struct.pack('<8s9I', b'EDINV2\0\0', 1251, 2, 121, 1, 256, 512, 2, 256, 512) + b'unchanged item records and Russian labels'
        changed, atlases = up.inventory_dimensions(raw)
        self.assertEqual(atlases, {1: (256, 512), 2: (256, 512)})
        self.assertEqual(changed[44:], raw[44:])
        self.assertEqual(struct.unpack_from('<III', changed, 20), (1, 512, 1024))

    def test_premultiplied_filter_does_not_add_black_border(self):
        im = Image.new('RGBA', (2, 1)); im.putpixel((0, 0), (255, 255, 255, 255))
        result = up.soft_rgba(im)
        self.assertEqual(result.size, (4, 2))
        self.assertTrue(all(r == 255 for r, g, b, a in result.getdata() if a))

    def test_derivative_rejects_script_or_depth_changes_and_unlisted_visuals(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); (root / 'provenance').mkdir()
            before = [dict(path=p, bytes=1, sha256='a' * 64) for p in ('scripts/levelA.jpc', 'aa01/background_0.depth', 'aa01/background_0.bmp')]
            base = dict(format=imp.FORMAT, status='verified-import', source_manifest_sha256=imp.SOURCE_SHA,
                        inputs_manifest_sha256='inputs', files=before, files_sha256=imp.digest(imp.json_bytes(before)))
            raw = imp.json_bytes(base); (root / up.BASE_RECORD).write_bytes(raw)
            receipt = dict(format=up.FORMAT, scale=2, base_manifest_path=up.BASE_RECORD,
                           base_manifest_sha256=imp.digest(raw), changed_files=[])
            manifest = dict(inputs_manifest_sha256='inputs', files=before + [dict(path=up.BASE_RECORD)], visual_upscale=receipt)
            up.verify_derivation(root, manifest)
            for name in ('scripts/levelA.jpc', 'aa01/background_0.depth', 'aa01/background_0.bmp'):
                after = [dict(r, sha256='b' * 64) if r['path'] == name else r for r in before]
                manifest['files'] = after + [dict(path=up.BASE_RECORD)]
                receipt['changed_files'] = [dict(path=name, source_sha256='a' * 64, sha256='b' * 64)]
                if name.endswith('.bmp'): up.verify_derivation(root, manifest)
                else:
                    with self.assertRaisesRegex(ValueError, 'Nonvisual'): up.verify_derivation(root, manifest)
            receipt['changed_files'] = []
            with self.assertRaisesRegex(ValueError, 'change table'): up.verify_derivation(root, manifest)

    def test_movie_overlay_preserves_timing_and_pcm_bytes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); native = root / 'native'; movies = root / 'movies'
            native.mkdir(); movies.mkdir(); header = struct.Struct('<8s10I2Q')
            rows = []; originals = []
            for identity in range(56):
                stem = f'{identity:08x}'
                old = header.pack(b'EDMOVIE1', 320, 240, 1, 30, 1, 44100, 2, 1, 320, 240, 0, 4) + b'PCM!'
                new = header.pack(b'EDMOVIE2', 640, 480, 1, 30, 1, 44100, 2, 1, 640, 480, 32, 4) + b'x' * 32 + b'PCM!'
                (native / (stem + '.edm')).write_bytes(old); (movies / (stem + '.edm')).write_bytes(new)
                original = dict(id=identity, decoded_sha256=imp.digest(old), source_name=stem + '.SFD', source_sha256='sfd')
                originals.append(original)
                row = dict(original, input_edm_sha256=imp.digest(old), decoded_sha256=imp.digest(new),
                           decoded_bytes=len(new), audio_pcm_sha256=imp.digest(b'PCM!'))
                rows.append(row); (movies / (stem + '.json')).write_bytes(imp.json_bytes(row))
            (native / 'manifest.json').write_bytes(imp.json_bytes(dict(movies=originals)))
            self.assertEqual(up.validate_movies(native, movies, rows, imp.Hashes())['exact_PCM_bytes'], 224)
            path = movies / '00000000.edm'; data = bytearray(path.read_bytes()); data[-1] ^= 1; path.write_bytes(data)
            rows[0]['decoded_sha256'] = imp.digest(data)
            (movies / '00000000.json').write_bytes(imp.json_bytes(rows[0]))
            with self.assertRaisesRegex(ValueError, 'PCM changed'):
                up.validate_movies(native, movies, rows, imp.Hashes())


if __name__ == '__main__': unittest.main()
