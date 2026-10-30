from pathlib import Path
import unittest

from ed_audio_spatial import radius_cm,spatial_percent,spatial_gain
from ed_scripts import ScriptPackage,_integer

ROOT=Path(__file__).resolve().parents[1]

class SpatialOracleTests(unittest.TestCase):
    def test_xz_only_distance_and_selected_radius(self):
        self.assertEqual(spatial_percent(100,4,(0,1000,0),(3,-1000,4)),75)
        self.assertEqual(spatial_percent(100,12,(0,0,0),(3,0,4),voice_radius=10),50)
        self.assertEqual(spatial_percent(100,4,(0,0,0),(0,0,10),voice_radius=1),50)
        self.assertEqual(spatial_percent(100,0,(0,0,0),(1000,0,1000)),100)
        self.assertEqual(spatial_percent(100,12,(0,0,0),(0,0,10),voice_radius=10),0)
        self.assertEqual(spatial_percent(100,4,(0,0,0),(0,0,20)),0)

    def test_truncation_radius_float_and_mute(self):
        self.assertEqual(spatial_percent(50,12,(0,0,0),(0,0,1),voice_radius=radius_cm(400)),37)
        self.assertEqual(radius_cm(400),4)
        self.assertLess(radius_cm(80),.8)
        self.assertEqual(spatial_percent(100,0x4004,(0,0,0),(0,0,0)),0)
        self.assertAlmostEqual(spatial_gain(100,4,(0,0,0),(0,0,10),master=50),.12)
        self.assertEqual(spatial_percent(100,12,(0,0,0),(0,0,1),voice_radius=0),0)
        with self.assertRaises(ArithmeticError):spatial_percent(100,12,(0,0,0),(0,0,0),voice_radius=0)
        with self.assertRaises(ValueError):spatial_percent(100,4,(float('nan'),0,0),(0,0,0))

@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class OriginalSpatialCallTests(unittest.TestCase):
    def test_both_source_packages_use_verified_wrapper_and_literal_positive_radii(self):
        for label,count in [('A',19),('B',9)]:
            package=ScriptPackage((ROOT/f'game/data/script/pc/level{label}.jpc').read_bytes())
            method=package.methods[172]
            self.assertEqual((method['name'],method['descriptor']),('soundPlay3D','(IIIII)I'))
            wrapper=package.disassemble(172)
            self.assertEqual([(i['offset'],i['method']['name']) for i in wrapper if 'method' in i],
                [(1,'sndMgrGetSNDID'),(6,'sndMgrPlaySoundVolLoop'),(20,'sndMgrSetVoicePositionMarker'),
                 (27,'sndMgrSetVoiceRadius'),(34,'sndMgrAddVoiceFlags')])
            self.assertEqual(_integer(next(i for i in wrapper if i['offset']==32)),12)
            calls=[]
            for owner in package.methods:
                ins=package.disassemble(owner['index'])
                for index,i in enumerate(ins):
                    if i.get('method',{}).get('index')==172:
                        args=[_integer(q) for q in ins[index-5:index]]
                        self.assertEqual(args[2],0)
                        self.assertGreater(args[4],0)
                        calls.append(args)
            self.assertEqual(len(calls),count)

if __name__=='__main__':unittest.main()
