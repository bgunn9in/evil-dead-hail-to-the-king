import shutil
import struct
import json
from pathlib import Path
import tempfile
import subprocess
import unittest
from unittest.mock import patch
import export_dreamcast_backgrounds as importer
from export_dreamcast_backgrounds import inspect_avi, read_pgmyuv, convert_rgb, mapping_gap

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'build/dreamcast-original-20261027/files'
CACHE=ROOT/'build/dc-native-backgrounds-20261028/native-output'
FFMPEG=Path(json.loads((CACHE/'source.json').read_text())['ffmpeg']['path']) if (CACHE/'source.json').is_file() else Path(shutil.which('ffmpeg') or 'missing-ffmpeg')

def chunk(tag, data):
    return tag+struct.pack('<I', len(data))+data+(b'\0' if len(data) & 1 else b'')

def avi():
    header = struct.pack('<10I', 33333, 0, 0, 0, 1, 0, 1, 0, 320, 240)
    stream = bytearray(b'vidsTM2X'+bytes(48))
    struct.pack_into('<I', stream, 20, 1)
    struct.pack_into('<I', stream, 24, 30)
    struct.pack_into('<I', stream, 32, 1)
    hdrl = chunk(b'LIST', b'hdrl'+chunk(b'avih', header)+chunk(b'LIST', b'strl'+chunk(b'strh', stream)))
    body = b'AVI '+hdrl+chunk(b'LIST', b'movi'+chunk(b'00dc', b'packet'))
    return b'RIFF'+struct.pack('<I', len(body))+body

class DecodeTests(unittest.TestCase):
    def test_original_game_destination_is_protected_before_any_input_or_process(self):
        target=ROOT/'game'/'__must_never_be_created__'
        self.assertFalse(target.exists())
        with patch.object(importer,'checked_inputs') as inputs,patch.object(importer.subprocess,'run') as run:
            with self.assertRaisesRegex(ValueError,'outside original'):
                importer.export(SOURCE,target,FFMPEG)
            with self.assertRaisesRegex(ValueError,'outside original'):
                importer.verify_cache(SOURCE,ROOT/'game')
            inputs.assert_not_called();run.assert_not_called()
        self.assertFalse(target.exists())

    def test_avi_bounds_and_frame_count(self):
        data = avi()
        self.assertEqual(inspect_avi(data)['packet_count'], 1)
        for bad in (b'', data[:-1], b'xxxx'+data[4:], data.replace(b'TM2X', b'XVID')):
            with self.assertRaises(ValueError):
                inspect_avi(bad)
        bad = bytearray(data); struct.pack_into('<I', bad, data.index(b'avih')+8+4*4, 2)
        with self.assertRaises(ValueError):
            inspect_avi(bad)

    def test_exact_yuvj444_carrier(self):
        pgm = b'P5\n320 720\n255\n'+bytes(320*240*3)
        self.assertEqual(len(read_pgmyuv(pgm)), 320*240*3)
        for bad in (pgm[:-1], pgm+b'\0', pgm.replace(b'720', b'240')):
            with self.assertRaises(ValueError):
                read_pgmyuv(bad)

    def test_missing_camera_track_stays_an_explicit_mapping_gap(self):
        self.assertIsNone(mapping_gap('AA01.AVI', 4, 4, [0, 1, 2, 3]))
        gap = mapping_gap('BG01.AVI', 18, 0, [])
        self.assertEqual(gap['camera_frames'], 0)
        self.assertIsNone(gap['max_camera_background_index'])
        self.assertIsNotNone(mapping_gap('BB01.AVI', 4, 3, [0, 1, 2]))

    @unittest.skipUnless(shutil.which('ffmpeg'), 'External FFmpeg is unavailable')
    def test_full_range_neutral_color_and_multiple_frames(self):
        count = 320*240
        planes = [bytes([y])*count+bytes([128])*(count*2) for y in (0, 128, 255)]
        result, _ = convert_rgb(planes, shutil.which('ffmpeg'))
        self.assertEqual(result, b''.join(bytes([y])*(count*3) for y in (0, 128, 255)))

    def test_altered_manifest_and_tool_are_rejected_without_execution(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);source=root/'files';source.mkdir()
            (root/'files-manifest.json').write_text('{}')
            with patch.object(importer.subprocess,'run') as run:
                with self.assertRaisesRegex(ValueError,'Unknown original'):
                    importer.checked_inputs(source)
                decoder=root/'decoder.exe';decoder.write_bytes(b'tampered')
                with self.assertRaisesRegex(ValueError,'NihAV decoder hash differs'):
                    importer.checked_tools(decoder,root/'ffmpeg.exe',root/'COPYING')
                run.assert_not_called()


@unittest.skipUnless((SOURCE/'AA01.AVI').is_file() and importer.DECODER.is_file() and FFMPEG.is_file() and (CACHE/'AA01/source.json').is_file(), 'Pinned native source/tools/cache required')
class OriginalImportTests(unittest.TestCase):
    def test_original_unknown_or_tampered_avi_is_refused_before_process(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);source=root/'files';source.mkdir()
            shutil.copyfile(SOURCE.parent/'files-manifest.json',root/'files-manifest.json')
            originals=json.loads((root/'files-manifest.json').read_text())['files']
            for item in originals:
                if item['path'].endswith('.AVI'):(source/item['path']).write_bytes(b'')
            for name in ('AA01.AVI','AA01V.DC','AA01.DC'):shutil.copyfile(SOURCE/name,source/name)
            data=(source/'AA01.AVI').read_bytes();(source/'AA01.AVI').write_bytes(bytes([data[0]^1])+data[1:])
            with patch.object(importer.subprocess,'run') as run:
                with self.assertRaisesRegex(ValueError,'Unknown original.*selection'):
                    importer.export(source,root/'unknown',FFMPEG,only=['XX99.AVI'])
                with self.assertRaisesRegex(ValueError,'Original resource hash differs'):
                    importer.export(source,root/'changed',FFMPEG,only=['AA01.AVI'])
                self.assertFalse((root/'unknown').exists());self.assertFalse((root/'changed').exists());run.assert_not_called()

    def test_failed_decode_is_atomic_and_existing_outputs_remain_immutable(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);output=root/'new'
            def fail(source,staging,*args):
                (staging/'partial.bmp').write_bytes(b'partial')
                raise RuntimeError('decoder failure fixture')
            with patch.object(importer,'_export_into',side_effect=fail):
                with self.assertRaisesRegex(RuntimeError,'decoder failure fixture'):
                    importer.export(SOURCE,output,FFMPEG,only=['AA01.AVI'])
            self.assertFalse(output.exists());self.assertEqual(list(root.iterdir()),[])
            output.mkdir();sentinel=output/'original';sentinel.write_bytes(b'immutable')
            with patch.object(importer,'_export_into') as decode:
                with self.assertRaises(FileExistsError):importer.export(SOURCE,output,FFMPEG)
                decode.assert_not_called()
            self.assertEqual(sentinel.read_bytes(),b'immutable')

    def test_external_decoder_error_never_publishes_partial_cache(self):
        real_run=importer.subprocess.run
        def fail_decoder(command,*args,**kwargs):
            if Path(command[0])==importer.DECODER:
                return subprocess.CompletedProcess(command,1,b'',b'forced decoder failure fixture')
            return real_run(command,*args,**kwargs)
        with tempfile.TemporaryDirectory() as temporary:
            output=Path(temporary)/'failed'
            with patch.object(importer.subprocess,'run',side_effect=fail_decoder):
                with self.assertRaisesRegex(ValueError,'forced decoder failure fixture'):
                    importer.export(SOURCE,output,FFMPEG,only=['AA01.AVI'])
            self.assertFalse(output.exists());self.assertEqual(list(Path(temporary).iterdir()),[])

    def test_fresh_original_aa01_decode_matches_all_four_immutable_frames(self):
        with tempfile.TemporaryDirectory() as temporary:
            output=Path(temporary)/'one-avi'
            result=importer.export(SOURCE,output,FFMPEG,only=['AA01.AVI'])
            self.assertEqual(result['conversion_status'],'SUBSET_PASS')
            self.assertFalse(result['complete_original_set']);self.assertEqual(result['decoded_frames'],4)
            self.assertEqual(result['decoder']['framework_commit'],importer.FRAMEWORK_COMMIT)
            self.assertEqual(result['decoder']['tool_commit'],importer.TOOL_COMMIT)
            old=json.loads((CACHE/'AA01/source.json').read_text())
            new=json.loads((output/'AA01/source.json').read_text())
            for a,b in zip(old['frames'],new['frames']):
                self.assertEqual({k:a[k] for k in ('pgmyuv_sha256','rgb_sha256','bmp_sha256')},{k:b[k] for k in ('pgmyuv_sha256','rgb_sha256','bmp_sha256')})
            self.assertFalse((output/'INCOMPLETE').exists())
            with self.assertRaisesRegex(ValueError,'complete pinned'):
                importer.verify_cache(SOURCE,output)

if __name__ == '__main__':
    unittest.main()
