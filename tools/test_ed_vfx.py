from pathlib import Path
import unittest

from ed_vfx import SourceRandom, ReferenceParticles, parse_effects, encode_effects, source_frame_requests

GAME = Path(__file__).resolve().parents[1] / "game"


class RandomTests(unittest.TestCase):
    def test_single_cached_services_share_seed_and_keep_separate_index(self):
        random = SourceRandom()
        self.assertEqual(random.bound(200), 156)
        seed = random.seed
        random.cached(6)
        self.assertNotEqual(random.seed, seed)
        seed = random.seed
        random.cached(6)
        self.assertEqual(random.seed, seed)
        random.bound(200)
        self.assertNotEqual(random.seed, seed)
        self.assertEqual(random.index, 2)


@unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
class NativeVfxTests(unittest.TestCase):
    def setUp(self):
        self.definitions = parse_effects((GAME / "EvilDead.exe").read_bytes())

    def test_source_constructor_constants_and_format(self):
        smoke, spark = self.definitions
        self.assertEqual((smoke["life"], smoke["cap"], smoke["size_min"], smoke["size_max"]), (40, 100, 30, 80))
        self.assertEqual((spark["life"], spark["growth"], spark["delta"]), (16, 12, [8, -4, -8, -16]))
        data = encode_effects(self.definitions)
        self.assertEqual(data[:8], b"EDVFX1\0\0")
        self.assertEqual(len(data), 20 + 2 * 88)

    def test_complete_scene_script_interleaves_actual_particle_rng(self):
        random = SourceRandom()
        particles = ReferenceParticles(self.definitions, random)
        calls = source_frame_requests(GAME, random, particles.request)
        self.assertEqual([call[0:3] for call in calls], [(9, 33, 33), (9, 57, 57)])
        self.assertEqual(len(particles.flat()), 2)
        self.assertEqual([kind for kind, bound in random.calls], ["single", "single", "single", "cached"] * 2)
        particles.update()
        self.assertEqual([p["age"] for p in particles.flat()], [1, 1])
        self.assertEqual([p["color"][3] for p in particles.flat()], [93, 93])
        for _ in range(41):
            particles.update()
        self.assertEqual(particles.flat(), [])

    def test_zero_size_spark_still_consumes_single_random_then_grows(self):
        random = SourceRandom()
        particles = ReferenceParticles(self.definitions, random)
        particles.spawn(23, (1, 2, 3))
        self.assertEqual(random.calls, [("single", 32767)])
        self.assertEqual(particles.flat()[0]["size"], 0)
        particles.update()
        self.assertEqual(particles.flat()[0]["size"], 12)
        self.assertEqual(particles.flat()[0]["color"], [136, 228, 247, 239])


if __name__ == "__main__":
    unittest.main()
