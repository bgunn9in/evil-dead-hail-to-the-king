import json
from pathlib import Path
import struct
from tempfile import TemporaryDirectory
import unittest
from upscale_dreamcast_movies import inspect_ivf, source_inputs, upscale, SOURCE_SHA


class UpscaleMovieTests(unittest.TestCase):
    def test_packet_ranges_timestamps_and_declared_count(self):
        with TemporaryDirectory() as directory:
            p=Path(directory)/"movie.ivf"
            header=struct.pack("<4sHH4sHHIIII",b"DKIF",0,32,b"AV01",640,480,30,1,2,0)
            valid=header+struct.pack("<IQ",3,0)+b"abc"+struct.pack("<IQ",2,1)+b"de"
            p.write_bytes(valid)
            self.assertEqual(inspect_ivf(p,640,480,2,(30,1)),3)
            bads=[valid[:-1],valid+b"extra",b"",valid[:32]]
            for offset,value in ((32,0),(32,16*1024**2+1),(36,1),(51,0)):
                bad=bytearray(valid);struct.pack_into("<I",bad,offset,value);bads.append(bad)
            for bad in bads:
                p.write_bytes(bad)
                with self.assertRaises(ValueError):
                    inspect_ivf(p,640,480,2,(30,1))
            p.write_bytes(valid)
            for w,h,n,fps in ((320,240,2,(30,1)),(640,480,3,(30,1)),(640,480,2,(60,1))):
                with self.assertRaises(ValueError):
                    inspect_ivf(p,w,h,n,fps)

    def fixture(self, directory):
        root=Path(directory);source=root/"native/movies";source.mkdir(parents=True)
        seal={"format":"ED-DREAMCAST-RUNTIME1","status":"verified-import","source_manifest_sha256":SOURCE_SHA}
        (source.parent/"dreamcast-runtime.json").write_text(json.dumps(seal))
        rows=[{"id":n} for n in range(56)]
        (source/"manifest.json").write_text(json.dumps({"format":"EDMOVIE1","movies":rows}))
        for row in rows:
            (source/f'{row["id"]:08x}.edm').write_bytes(b"not read by path validation")
        return source,root/"output",seal

    def test_original_source_and_output_guards_before_encoding(self):
        with TemporaryDirectory() as directory:
            source,output,seal=self.fixture(directory)
            self.assertEqual(source_inputs(source,output)[:2],(source.resolve(),output.resolve()))
            for destination in (source,source/"out",source.parent/"out",source.parent.parent):
                with self.assertRaises((ValueError,FileExistsError)):
                    source_inputs(source,destination)
            for field,value in (("status","pending"),("source_manifest_sha256","0"*64),("visual_upscale",{})):
                changed={**seal,field:value};(source.parent/"dreamcast-runtime.json").write_text(json.dumps(changed))
                with self.assertRaises(ValueError):
                    source_inputs(source,output)
                self.assertFalse(output.exists())
            for workers in (True,0,5):
                with self.assertRaises(ValueError):
                    upscale(source,output,workers)

    def test_linked_movie_input_is_not_accepted(self):
        with TemporaryDirectory() as directory:
            source,output,seal=self.fixture(directory)
            victim=source/"00000000.edm";victim.unlink()
            try:
                victim.symlink_to(source/"00000001.edm")
            except OSError:
                self.skipTest("Host does not permit synthetic symlinks")
            with self.assertRaises(ValueError):
                source_inputs(source,output)


if __name__=="__main__":
    unittest.main()
