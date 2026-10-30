"""Build the SDL GPU shaders with pinned offline DXC, checking bindings.

The generated HLSL copy adds Vulkan combined-image annotations. The shared
HLSL and unrelated output files are never modified. DXC validates the SPIR-V;
the small reader below checks this project's SDL resource ABI, not all SPIR-V.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DXC_VERSION = "1.9.2609"
DXC_URL = "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip"
DXC_ARCHIVE_SHA256 = "ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1"
DXC_FILES = {
    "dxc.exe": "aa7965b3763d9c710baaa5a28cedcc633449962a75d1391d161a85304a41b3e2",
    "dxcompiler.dll": "0689e2fee0a1c9df8662a59680469f24e2a3ad7ce6bbbcc9284f7a83172bfc59",
    "dxil.dll": "64be2368211d257a038a7d4ea6bb3e24c878eb8d588c7b8414fccf759b69c2ac",
}
STAGES = (
    ("vert", "VSMain", "vs_6_0", 0, 0, 1),
    ("frag", "PSMain", "ps_6_0", 4, 1, 1),
    ("bgfrag", "PSBackground", "ps_6_0", 4, 2, 1),
    ("hudfrag", "PSHud", "ps_6_0", 4, 1, 1),
    ("particlefrag", "PSParticle", "ps_6_0", 4, 1, 1),
    ("opaquefrag", "PSOpaque", "ps_6_0", 4, 1, 1),
    ("maskfrag", "PSDCMask", "ps_6_0", 4, 2, 1),
    ("postfrag", "PSPost", "ps_6_0", 4, 1, 1),
)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def string_words(words):
    raw = struct.pack(f"<{len(words)}I", *words)
    end = raw.find(b"\0")
    if end < 0:
        raise ValueError("Unterminated SPIR-V string")
    return raw[:end].decode("utf-8")


def reflect(data):
    if len(data) < 20 or len(data) % 4 or len(data) > 1024 * 1024:
        raise ValueError("Invalid SPIR-V byte count")
    words = struct.unpack(f"<{len(data) // 4}I", data)
    if words[0] != 0x07230203 or words[1] != 0x00010000 or words[4] or not 0 < words[3] <= 1000000:
        raise ValueError("Unexpected SPIR-V1.0 header")
    decorations, members, pointers, variables, types, entries = {}, {}, {}, {}, {}, []
    offset = 5
    while offset < len(words):
        count, opcode = words[offset] >> 16, words[offset] & 65535
        if count < 1 or count > len(words) - offset:
            raise ValueError("Truncated SPIR-V instruction")
        args = words[offset + 1:offset + count]
        if opcode == 15:
            if len(args) < 3:
                raise ValueError("Truncated entry point")
            entries.append({"stage": args[0], "entry": string_words(args[2:])})
        elif opcode == 71:
            if len(args) < 2:
                raise ValueError("Truncated decoration")
            decorations.setdefault(args[0], {})[args[1]] = list(args[2:])
        elif opcode == 72:
            if len(args) < 3:
                raise ValueError("Truncated member decoration")
            members.setdefault(args[0], {}).setdefault(args[1], {})[args[2]] = list(args[3:])
        elif opcode == 32:
            if len(args) != 3:
                raise ValueError("Truncated pointer type")
            pointers[args[0]] = (args[1], args[2])
        elif opcode == 59:
            if len(args) < 3:
                raise ValueError("Truncated variable")
            variables[args[1]] = (args[0], args[2])
        elif opcode in (25, 26, 27, 30):
            if not args:
                raise ValueError("Truncated shader type")
            types[args[0]] = opcode
        offset += count
    resources, inputs = [], []
    for variable, (pointer, storage) in variables.items():
        dec = decorations.get(variable, {})
        if storage == 1 and 30 in dec:
            if len(dec[30]) != 1:
                raise ValueError("Incomplete shader input location")
            inputs.append(dec[30][0])
        if 33 not in dec and 34 not in dec:
            continue
        if len(dec.get(33, [])) != 1 or len(dec.get(34, [])) != 1 or pointer not in pointers:
            raise ValueError("Incomplete shader resource binding")
        pointer_storage, kind = pointers[pointer]
        if pointer_storage != storage:
            raise ValueError("Shader pointer storage mismatch")
        if storage == 0 and types.get(kind) == 27:
            resource = {"kind": "combined-image-sampler"}
        elif storage == 2 and types.get(kind) == 30 and 2 in decorations.get(kind, {}):
            fields = members.get(kind, {})
            if any(len(v.get(35, [])) != 1 or (7 in v and len(v[7]) != 1) for v in fields.values()):
                raise ValueError("Incomplete uniform member layout")
            resource = {"kind": "uniform", "offsets": [fields[i].get(35, []) for i in sorted(fields)]}
            resource["matrix_strides"] = [v[7][0] for v in fields.values() if 7 in v]
        else:
            raise ValueError("Shader resource is not an SDL combined sampler or uniform")
        resource.update(set=dec[34][0], binding=dec[33][0])
        resources.append(resource)
    return {"entries": entries, "inputs": sorted(inputs), "resources": sorted(resources, key=lambda r: (r["set"], r["binding"]))}


def validate(reflection, stage):
    suffix, entry, _, model, samplers, uniforms = stage
    expected = [(2, i, "combined-image-sampler") for i in range(samplers)]
    if uniforms:
        expected.append((1 if model == 0 else 3, 0, "uniform"))
    actual = [(r["set"], r["binding"], r["kind"]) for r in reflection["resources"]]
    if reflection["entries"] != [{"stage": model, "entry": entry}] or sorted(actual) != sorted(expected):
        raise ValueError(f"{suffix}: shader resource ABI differs from SDL GPU contract")
    if model == 0 and reflection["inputs"] != [0, 1, 2]:
        raise ValueError("Vertex input locations differ from existing Vertex32")
    for resource in reflection["resources"]:
        if resource["kind"] == "uniform":
            offsets = [[0], [64]] if model == 0 else [[0], [16], [32]] if suffix == "postfrag" else [[0], [16]] if suffix == "particlefrag" else [[0]]
            strides = [16] if model == 0 else []
            if resource["offsets"] != offsets or resource["matrix_strides"] != strides:
                raise ValueError(f"{suffix}: uniform layout differs from CPU data")


def annotated_source(source):
    result, count = re.subn(r"(?m)^((?:Texture2D(?:<[^>]+>)?|SamplerState)\s+\w+\s*:\s*register\([^\n]+\);)",
                            r"[[vk::combinedImageSampler]]\n\1", source)
    if count != 4:
        raise ValueError("Expected the existing two paired textures/samplers; review new resources explicitly")
    return result


def build(source, output, dxc):
    source, output, dxc = source.resolve(), output.resolve(), dxc.resolve()
    if output.is_relative_to((ROOT / "game").resolve()):
        raise ValueError("Shader outputs cannot overwrite original game data")
    for name, expected in DXC_FILES.items():
        if sha256((dxc.parent / name).read_bytes()) != expected:
            raise ValueError(f"Expected pinned official DXC{DXC_VERSION}: {name}")
    original = source.read_bytes()
    generated = annotated_source(original.decode("utf-8"))
    output.mkdir(parents=True, exist_ok=True)
    manifest = {"format": "EDSPIRV1", "source_sha256": sha256(original), "generated_sha256": sha256(generated.encode()),
                "compiler": {"version": DXC_VERSION, "url": DXC_URL, "archive_sha256": DXC_ARCHIVE_SHA256, "files": DXC_FILES}, "shaders": []}
    with tempfile.TemporaryDirectory(prefix=".spirv-", dir=output) as scratch:
        scratch = Path(scratch)
        temporary = scratch / "scene.hlsl"
        temporary.write_text(generated, encoding="utf-8", newline="\n")
        for stage in STAGES:
            suffix, entry, profile, *_ = stage
            destination = scratch / f"scene.{suffix}.spv"
            command = [str(dxc), "-spirv", "-T", profile, "-E", entry, "-fspv-target-env=vulkan1.0", "-fvk-use-dx-layout",
                       "-fvk-stage-io-order=decl", "-O3", "-I", str(source.parent), "-Fo", str(destination), str(temporary)]
            if suffix == "postfrag":
                command[1:1] = ["-D", "ED_POST=1"]
            if suffix == "particlefrag":
                command[1:1] = ["-D", "ED_PARTICLE=1"]
            subprocess.run(command, check=True, capture_output=True, text=True)
            data = destination.read_bytes()
            reflection = reflect(data)
            validate(reflection, stage)
            manifest["shaders"].append({"file": destination.name, "sha256": sha256(data), "bytes": len(data), **reflection})
        (scratch / "spirv-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        for shader in manifest["shaders"]:
            os.replace(scratch / shader["file"], output / shader["file"])
        os.replace(scratch / "spirv-manifest.json", output / "spirv-manifest.json")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "runtime/scene.hlsl")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--dxc", type=Path, default=ROOT / "build/dependencies/DXC-1.9.2609/bin/x64/dxc.exe")
    args = parser.parse_args()
    try:
        result = build(args.source, args.output, args.dxc)
        print("Verified SDL GPU SPIR-V:", ", ".join(shader["file"] for shader in result["shaders"]))
    except subprocess.CalledProcessError as error:
        parser.exit(1, f"Shader build failed:\n{error.stderr or error.stdout or error}\n")
    except (OSError, ValueError) as error:
        parser.exit(1, f"Shader build failed: {error}\n")
