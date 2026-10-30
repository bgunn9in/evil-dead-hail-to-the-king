import unittest
from fractions import Fraction
from ed_audio_gain import cached_percent, source_gain

class SourceGainTests(unittest.TestCase):
    def test_master_goldens_and_byte_conversion(self):
        for volume, master, cached, gain in [(100,0,0,0),(100,37,36,.1332),(21,37,7,.0259),
                (100,50,49,.245),(50,50,24,.12),(50,100,50,.5),(50,101,50,.505)]:
            self.assertEqual(cached_percent(volume,master),cached)
            self.assertAlmostEqual(source_gain(volume,master),gain,places=12)
            self.assertEqual(source_gain(volume,master+256),source_gain(volume,master))
        self.assertEqual(source_gain(21,-1),source_gain(21,255))
        self.assertEqual(source_gain(-1),0)
        self.assertAlmostEqual(source_gain(100),10**(-458/2000))

    def test_all_bounded_values_match_exact_float32_literal_product(self):
        factor=Fraction(5368709,1<<29)
        for master in range(256):
            for volume in range(101):
                expected=int(volume*master*factor) if master<100 else volume
                self.assertEqual(cached_percent(volume,master),expected)
                self.assertGreaterEqual(source_gain(volume,master),0)
                self.assertLessEqual(source_gain(volume,master),10**(-458/2000))

if __name__=='__main__':unittest.main()
