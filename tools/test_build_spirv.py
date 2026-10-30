import copy
from pathlib import Path
import subprocess
import tempfile
import unittest

from build_spirv import ROOT, STAGES, annotated_source, build, reflect, validate


class ShaderContractTests(unittest.TestCase):
    def test_truncated_binary(self):
        for data in (b"", b"\x03\x02\x23\x07" * 5, b"123"):
            with self.assertRaises(ValueError):
                reflect(data)

    def test_sampler_must_be_combined_and_in_fragment_set(self):
        valid = {"entries": [{"stage": 4, "entry": "PSMain"}], "inputs": [0, 1],
                 "resources": [{"kind": "combined-image-sampler", "set": 2, "binding": 0},
                               {"kind": "uniform", "set": 3, "binding": 0, "offsets": [[0]], "matrix_strides": []}]}
        validate(valid, STAGES[1])
        for field, value in (("set", 0), ("binding", 1), ("kind", "separate-image")):
            invalid = copy.deepcopy(valid)
            invalid["resources"][0][field] = value
            with self.assertRaises(ValueError):
                validate(invalid, STAGES[1])

    def test_post_uniform_requires_three_float4s(self):
        valid = {"entries": [{"stage": 4, "entry": "PSPost"}], "inputs": [],
                 "resources": [{"kind": "combined-image-sampler", "set": 2, "binding": 0},
                               {"kind": "uniform", "set": 3, "binding": 0, "offsets": [[0], [16], [32]], "matrix_strides": []}]}
        validate(valid, STAGES[-1])
        for offsets in ([[0]], [[0], [16]], [[0], [12]], [[16], [0]], [[0], [16], [28]]):
            wrong = copy.deepcopy(valid)
            wrong["resources"][1]["offsets"] = offsets
            with self.assertRaises(ValueError):
                validate(wrong, STAGES[-1])

    def test_particle_uniform_requires_color_and_half_open_depth_band(self):
        valid = {"entries": [{"stage": 4, "entry": "PSParticle"}], "inputs": [],
                 "resources": [{"kind": "combined-image-sampler", "set": 2, "binding": 0},
                               {"kind": "uniform", "set": 3, "binding": 0, "offsets": [[0], [16]], "matrix_strides": []}]}
        validate(valid, STAGES[4])
        for offsets in ([[0]], [[0], [12]], [[16], [0]], [[0], [16], [32]]):
            wrong = copy.deepcopy(valid)
            wrong["resources"][1]["offsets"] = offsets
            with self.assertRaises(ValueError):
                validate(wrong, STAGES[4])

    def test_new_source_resources_require_review(self):
        with self.assertRaises(ValueError):
            annotated_source("Texture2D image : register(t0, space2);\n")

    @unittest.skipUnless((ROOT / "build/dependencies/DXC-1.9.2609/bin/x64/dxc.exe").exists(), "Pinned Windows DXC is not present")
    def test_failed_compile_preserves_previous_outputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / "broken.hlsl"
            source.write_text((ROOT / "runtime/scene.hlsl").read_text() + "\ninvalid HLSL syntax here;\n")
            previous = {f"scene.{stage[0]}.spv": b"previous" for stage in STAGES}
            previous.update({"spirv-manifest.json": b"previous manifest", "unrelated.bin": b"unrelated output"})
            for name, value in previous.items():
                (directory / name).write_bytes(value)
            with self.assertRaises(subprocess.CalledProcessError):
                build(source, directory, ROOT / "build/dependencies/DXC-1.9.2609/bin/x64/dxc.exe")
            for name, value in previous.items():
                self.assertEqual((directory / name).read_bytes(), value)


if __name__ == "__main__":
    unittest.main()
