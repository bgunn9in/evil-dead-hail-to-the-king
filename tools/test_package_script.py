"""Reject incomplete A/B packages before creating or copying any output."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
POWERSHELL = shutil.which("powershell") or shutil.which("pwsh")
# PowerShell 7's inherited module path cannot load Windows PowerShell's utilities.
POWERSHELL_ENV = {key: value for key, value in os.environ.items() if key.lower() != "psmodulepath"}


@unittest.skipUnless(os.name == "nt" and POWERSHELL, "Windows PowerShell package")
class PackageScriptTests(unittest.TestCase):
    def seal_fixture(self,root,build):
        # Real public read-only validator; synthetic fixtures exercise packaging
        # contracts only and make no claim that stub binaries/assets are playable.
        tools=root/'tools';tools.mkdir(exist_ok=True)
        for name in ('import_dreamcast_runtime.py','ed_dc_audio.py','ed_audio.py','ed_assets.py','ed_movies.py'):
            shutil.copyfile(ROOT/'tools'/name,tools/name)
        assets=build/'assets';inputs=assets/'provenance/import-inputs.json';inputs.parent.mkdir(parents=True,exist_ok=True);inputs.write_text('{}')
        records=[dict(path=f.relative_to(assets).as_posix(),bytes=f.stat().st_size,sha256=hashlib.sha256(f.read_bytes()).hexdigest())
                 for f in sorted(assets.rglob('*')) if f.is_file() and f.name!='dreamcast-runtime.json']
        encoded=lambda value:(json.dumps(value,indent=2,ensure_ascii=False)+'\n').encode('utf-8')
        seal=dict(format='ED-DREAMCAST-RUNTIME1',status='verified-import',source_manifest_sha256='cb0c24fee804eb7cc7bc65f4e2311f7498ab1c075f12f7c8cc7f5ff1d4cbc9c9',
            original_script_sha256={level:hashlib.sha256((assets/('' if level=='A' else 'levelB')/f'scripts/level{level}.jpc').read_bytes()).hexdigest()
                if (assets/('' if level=='A' else 'levelB')/f'scripts/level{level}.jpc').is_file() else '0'*64 for level in 'AB'},
            inputs_manifest_sha256=hashlib.sha256(inputs.read_bytes()).hexdigest(),files=records,file_count=len(records),files_sha256=hashlib.sha256(encoded(records)).hexdigest())
        (assets/'dreamcast-runtime.json').write_bytes(encoded(seal))

    def complete_fixture(self, root):
        script = root / "scripts/package.ps1"
        script.parent.mkdir()
        shutil.copyfile(ROOT / "scripts/package.ps1", script)
        runtime = root / "runtime"
        runtime.mkdir()
        (runtime / "main.cpp").write_text("// package fixture; no executable is run\n")
        shutil.copyfile(ROOT / "runtime/scene.hlsl", runtime / "scene.hlsl")
        for name in ("flac_decode.cpp", "flac_decode.h", "third_party/dr_flac.h", "third_party/dr_flac-LICENSE.txt", "third_party/dr_flac-pin.json"):
            target = runtime / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / "runtime" / name, target)
        (root / "research").mkdir()
        shutil.copyfile(ROOT / "README-package.md", root / "README-package.md")
        build = root / "build"

        def write(name, data=b"fixture"):
            path = build / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            return path

        # These bounded PE headers exercise the existing x64 package check;
        # this unit fixture makes no claim that its stub binaries are runnable.
        pe = bytearray(256)
        struct.pack_into("<H", pe, 0, 0x5A4D)
        struct.pack_into("<I", pe, 0x3C, 0x80)
        struct.pack_into("<IH", pe, 0x80, 0x4550, 0x8664)
        binary = write("Release/evil-dead-scene.exe", pe)
        write("Release/SDL3.dll", pe)
        crt = root / "VC/Redist/MSVC/1.0/x64/Microsoft.VC143.CRT"
        crt.mkdir(parents=True)
        for name in ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"):
            (crt / name).write_bytes(pe)
        for prefix, level, menu in (("assets/", "A", "aa01"), ("assets/levelB/", "B", "bb01")):
            scenes = [f"{level.lower()}k{n:02d}" for n in range(1, 9)]
            write(prefix + "world.txt", ("EDWORLD1\n" + scenes[0] + "\n8\n" + "\n".join(scenes) + "\n").encode())
            write(prefix + "episode.json", json.dumps({"scenes": scenes}).encode())
            for name in ("actors/ash/actor-parts.bin", "audio/manifest.txt", "movies/manifest.json", "objects/source.json", "vfx/source.json",
                         f"scripts/level{level}.jpc", "gui/text.bin", "gui/layout.bin", "gui/models/game-over.mesh", "gui/textures/0b942034.rgba", "gui/source.json"):
                write(prefix + name)
            for name in ("menu.txt", "scene.native", "menu-resources.bin", "camera-frames.bin", "background_0.bmp"):
                write(prefix + menu + "/" + name)
            for scene in scenes:
                for name in ("scene.txt", "scene.native", "portals.bin"):
                    write(prefix + scene + "/" + name)
        manifest = json.loads((ROOT / "runtime/shaders/spirv-manifest.json").read_text())
        self.assertEqual(len(manifest["shaders"]), 8)
        self.assertEqual(manifest["shaders"][7]["file"], "scene.postfrag.spv")
        post_uniform = next(r for r in manifest["shaders"][7]["resources"] if r["kind"] == "uniform")
        self.assertEqual(post_uniform["offsets"], [[0], [16], [32]])
        for folder in (runtime / "shaders", build / "shaders"):
            folder.mkdir()
            for name in [s["file"] for s in manifest["shaders"]] + ["spirv-manifest.json"]:
                shutil.copyfile(ROOT / "runtime/shaders" / name, folder / name)
        write("shaders/unexpected.spv", b"unlisted shader must not be packaged")
        write("shaders/unrelated.bin", b"not a Vulkan shader input")
        license = root / "license.txt"
        license.write_text("unit fixture license\n")
        write("dependencies/SDL3-3.4.18-vulkan/vulkan-sdk-stamp.json", json.dumps({
            "files": [{"path": "bin/SDL3.dll", "sha256": hashlib.sha256(pe).hexdigest()},
                      {"path": "licenses/SDL3/LICENSE.txt", "sha256": hashlib.sha256(license.read_bytes()).hexdigest()}]
        }).encode())
        dav1d = "dependencies/dav1d-1.5.2/"
        library = write(dav1d+"install/lib/libdav1d.a", b"static decoder unit fixture")
        decoder_license = write(dav1d+"install/licenses/dav1d-COPYING.txt", b"BSD-2-Clause unit fixture notice")
        write(dav1d+"sdk-stamp.json", json.dumps({
            "fingerprint": "dav1d1.5.2|x64|MSVC|fixture", "license": "BSD-2-Clause",
            "archive_sha256": "2fc0810b4cdf72784b3c107827ff10b1d83ec709a1ec1fbdbc6a932daf65ead6",
            "files": [{"path": "lib/libdav1d.a", "sha256": hashlib.sha256(library.read_bytes()).hexdigest()},
                      {"path": "licenses/dav1d-COPYING.txt", "sha256": hashlib.sha256(decoder_license.read_bytes()).hexdigest()}]
        }).encode())
        os.utime(binary, (time.time() + 120, time.time() + 120))
        self.seal_fixture(root,build)
        return script, build, license, crt, root / "package"

    def package_fixture(self, fixture):
        script, build, license, crt, output = fixture
        return subprocess.run([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script),
                               "-BuildDirectory", str(build), "-OutputDirectory", str(output),
                               "-SdlLicensePath", str(license), "-CrtDirectory", str(crt)],
                              capture_output=True, text=True, timeout=30, env=POWERSHELL_ENV)

    def test_vulkan_only_package_omits_stale_shader_outputs(self):
        with TemporaryDirectory() as temporary:
            fixture = self.complete_fixture(Path(temporary))
            process = self.package_fixture(fixture)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            output = fixture[-1]
            expected = {s["file"] for s in json.loads((ROOT / "runtime/shaders/spirv-manifest.json").read_text())["shaders"]}
            expected.add("spirv-manifest.json")
            self.assertEqual({p.name for p in (output / "bin/shaders").iterdir()}, expected)
            self.assertFalse((output / "INCOMPLETE.txt").exists())
            manifest = json.loads((output / "package-manifest.json").read_text())
            self.assertFalse(any(f["file"].endswith(("unexpected.spv", "unrelated.bin")) for f in manifest["files"]))
            notice=(output/'NOTICE.txt').read_text(encoding='utf-8')
            self.assertIn('Original resource platform: Sega Dreamcast',notice)
            self.assertIn('Original LEVELA.JDC SHA256',notice)
            self.assertIn('dav1d1.5.2',notice)
            self.assertTrue((output/'licenses/dav1d-COPYING.txt').is_file())
            self.assertTrue((output/'licenses/dav1d-sdk.json').is_file())
            self.assertFalse(any('dav1d' in p.name.lower() for p in (output/'bin').iterdir()))
            self.assertNotIn('Original executable version',notice)
            self.assertFalse((Path(temporary)/'game').exists())

    def test_native_seal_or_asset_tampering_never_creates_output(self):
        for broken in ('missing','status','provenance','file','extra','seal'):
            with self.subTest(broken=broken), TemporaryDirectory() as temporary:
                fixture=self.complete_fixture(Path(temporary));assets=fixture[1]/'assets';seal=assets/'dreamcast-runtime.json'
                if broken=='missing':seal.unlink()
                elif broken=='status':
                    data=json.loads(seal.read_text());data['status']='INCOMPLETE';seal.write_text(json.dumps(data))
                elif broken=='provenance':
                    data=json.loads(seal.read_text());data['source_manifest_sha256']='0'*64;seal.write_text(json.dumps(data))
                elif broken=='file':(assets/'gui/text.bin').write_bytes(b'damaged')
                elif broken=='extra':(assets/'unsealed.bin').write_bytes(b'extra')
                else:
                    data=json.loads(seal.read_text());data['files'][0]['sha256']='0'*64;seal.write_text(json.dumps(data))
                process=self.package_fixture(fixture)
                self.assertNotEqual(process.returncode,0,process.stdout+process.stderr)
                self.assertFalse(fixture[-1].exists())

    def test_unrelated_research_is_not_bundled_as_native_provenance(self):
        with TemporaryDirectory() as temporary:
            root=Path(temporary);fixture=self.complete_fixture(root);(root/'research/old-pc.json').write_text('{"unrelated":"research"}')
            process=self.package_fixture(fixture);self.assertEqual(process.returncode,0,process.stdout+process.stderr)
            self.assertFalse((fixture[-1]/'provenance/old-pc.json').exists())
            self.assertTrue((fixture[-1]/'assets/provenance/import-inputs.json').is_file())

    def test_asset_changed_after_preflight_fails_copy_against_original_seal(self):
        with TemporaryDirectory() as temporary:
            root=Path(temporary);fixture=self.complete_fixture(root);script,build,license,crt,output=fixture
            wrapper=root/'change-after-verify.ps1'
            wrapper.write_text('''
function global:python {
    & $env:PACKAGE_REAL_PYTHON @args
    if($LASTEXITCODE -ne 0){throw 'Real native verifier failed'}
    [IO.File]::AppendAllText($env:PACKAGE_CHANGE_FILE,'changed-after-verification')
    $global:LASTEXITCODE=0
}
& $env:PACKAGE_SCRIPT -BuildDirectory $env:PACKAGE_BUILD -OutputDirectory $env:PACKAGE_OUTPUT -SdlLicensePath $env:PACKAGE_LICENSE -CrtDirectory $env:PACKAGE_CRT
''',encoding='utf-8')
            env={**POWERSHELL_ENV,'PACKAGE_REAL_PYTHON':sys.executable,'PACKAGE_CHANGE_FILE':str(build/'assets/gui/text.bin'),
                'PACKAGE_SCRIPT':str(script),'PACKAGE_BUILD':str(build),'PACKAGE_OUTPUT':str(output),'PACKAGE_LICENSE':str(license),'PACKAGE_CRT':str(crt)}
            process=subprocess.run([POWERSHELL,'-NoProfile','-ExecutionPolicy','Bypass','-File',str(wrapper)],capture_output=True,text=True,timeout=30,env=env)
            self.assertNotEqual(process.returncode,0)
            self.assertIn('Copied bytes differ',process.stdout+process.stderr)
            self.assertTrue((output/'INCOMPLETE.txt').exists());self.assertFalse((output/'package-manifest.json').exists())

    def test_mismatched_sdl_never_creates_output(self):
        with TemporaryDirectory() as temporary:
            fixture = self.complete_fixture(Path(temporary))
            with (fixture[1] / "Release/SDL3.dll").open("ab") as stream:
                stream.write(b"stale library")
            process = self.package_fixture(fixture)
            self.assertNotEqual(process.returncode, 0)
            self.assertIn("verified Windows Vulkan SDK", process.stderr)
            self.assertFalse(fixture[-1].exists())

    def test_dav1d_pin_or_static_inputs_reject_before_output(self):
        for broken in ("pin", "library", "license", "stamp"):
            with self.subTest(broken=broken), TemporaryDirectory() as temporary:
                fixture=self.complete_fixture(Path(temporary))
                sdk=fixture[1]/"dependencies/dav1d-1.5.2"
                if broken=="pin":
                    p=sdk/"sdk-stamp.json";data=json.loads(p.read_text());data["archive_sha256"]="0"*64;p.write_text(json.dumps(data))
                elif broken=="stamp":
                    (sdk/"sdk-stamp.json").unlink()
                else:
                    p=sdk/("install/lib/libdav1d.a" if broken=="library" else "install/licenses/dav1d-COPYING.txt")
                    p.write_bytes(p.read_bytes()+b"changed")
                process=self.package_fixture(fixture)
                self.assertNotEqual(process.returncode,0)
                self.assertIn("dav1d",process.stdout+process.stderr)
                self.assertFalse(fixture[-1].exists())

    def test_mismatched_sdl_license_never_creates_output(self):
        with TemporaryDirectory() as temporary:
            fixture = self.complete_fixture(Path(temporary))
            fixture[2].write_text("unrelated document\n")
            process = self.package_fixture(fixture)
            self.assertNotEqual(process.returncode, 0)
            self.assertIn("SDL license differs", process.stderr)
            self.assertFalse(fixture[-1].exists())

    def test_invalid_vulkan_inputs_never_create_output(self):
        for broken in ("checksum", "source", "abi", "post-count", "post-entry", "post-sampler", "post-uniform", "post-legacy-uniform"):
            with self.subTest(broken=broken), TemporaryDirectory() as temporary:
                root = Path(temporary)
                fixture = self.complete_fixture(root)
                if broken == "checksum":
                    path = fixture[1] / "shaders/scene.frag.spv"
                    data = bytearray(path.read_bytes())
                    data[-1] ^= 1
                    path.write_bytes(data)
                    expected = "SPIR-V shader checksum"
                elif broken == "source":
                    with (root / "runtime/scene.hlsl").open("a") as out:
                        out.write("\n// changed source\n")
                    expected = "SPIR-V source manifest is stale"
                else:
                    source = root / "runtime/shaders/spirv-manifest.json"
                    manifest = json.loads(source.read_text())
                    expected = "SPIR-V SDL resource ABI differs"
                    if broken == "abi":
                        manifest["shaders"][0]["resources"][0]["offsets"] = [[4], [64]]
                        expected = "SPIR-V uniform ABI differs"
                    elif broken == "post-count":
                        manifest["shaders"].pop()
                        expected = "SPIR-V source manifest is stale or invalid"
                    elif broken == "post-entry":
                        manifest["shaders"][7]["entries"][0]["entry"] = "PSMain"
                    elif broken == "post-sampler":
                        sampler = next(r for r in manifest["shaders"][7]["resources"] if r["kind"] == "combined-image-sampler")
                        sampler["binding"] = 1
                        expected = "SPIR-V sampler ABI differs"
                    else:
                        uniform = next(r for r in manifest["shaders"][7]["resources"] if r["kind"] == "uniform")
                        uniform["offsets"] = [[0], [16]] if broken == "post-legacy-uniform" else [[0], [16], [36]]
                        expected = "SPIR-V uniform ABI differs"
                    source.write_text(json.dumps(manifest))
                    shutil.copyfile(source, fixture[1] / "shaders/spirv-manifest.json")
                process = self.package_fixture(fixture)
                self.assertNotEqual(process.returncode, 0)
                self.assertIn(expected, process.stdout + process.stderr)
                self.assertFalse(fixture[-1].exists())

    def test_freshness_follows_includes_and_ignores_uncompiled_experiments(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            script = root / "scripts/package.ps1"
            script.parent.mkdir()
            shutil.copyfile(ROOT / "scripts/package.ps1", script)
            runtime = root / "runtime"
            runtime.mkdir()
            (runtime / "main.cpp").write_text('#include "used.h"\n')
            codec = runtime / "flac_decode.cpp"
            codec.write_text('// separately compiled lossless codec\n')
            used, unused = runtime / "used.h", runtime / "unused.inl"
            used.write_text("// included\n")
            unused.write_text("// independent experiment\n")
            build = root / "build"
            for name in ("Release/evil-dead-scene.exe", "Release/SDL3.dll", "assets/world.txt", "assets/scripts/levelA.jpc",
                         "assets/actors/ash/actor-parts.bin", "assets/audio/manifest.txt", "assets/movies/manifest.json",
                         "assets/objects/source.json", "assets/vfx/source.json", "shaders/spirv-manifest.json"):
                path = build / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"fixture")
            license = root / "license.txt"
            license.write_text("fixture")
            stamp = time.time()
            os.utime(build / "Release/evil-dead-scene.exe", (stamp + 10, stamp + 10))
            os.utime(unused, (stamp + 60, stamp + 60))
            self.seal_fixture(root,build)
            for stale in (None, used, codec):
                os.utime(used, (stamp, stamp))
                os.utime(codec, (stamp, stamp))
                if stale:
                    os.utime(stale, (stamp + 20, stamp + 20))
                process = subprocess.run([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script),
                                          "-BuildDirectory", str(build), "-OutputDirectory", str(root / "package"), "-SdlLicensePath", str(license)],
                                         capture_output=True, text=True, timeout=30, env=POWERSHELL_ENV)
                self.assertNotEqual(process.returncode, 0)
                self.assertIn("Runtime sources are newer" if stale else "Missing package profile input", process.stdout + process.stderr)
                self.assertFalse((root / "package").exists())

    def test_incomplete_second_package_never_creates_output(self):
        for broken in ("world", "menu", "metadata"):
            with self.subTest(broken=broken), TemporaryDirectory() as temporary:
                root = Path(temporary)
                fixture=self.complete_fixture(root);build=fixture[1]
                if broken == "world":
                    (build / "assets/levelB/world.txt").unlink()
                    expected = "Missing package profile input"
                elif broken == "menu":
                    (build / "assets/levelB/bb01/menu-resources.bin").unlink()
                    expected = "Missing package menu/script input"
                else:
                    (build/'assets/levelB/episode.json').write_bytes(b'{"scenes": []}')
                    expected = "Episode metadata does not match"
                output=fixture[-1];process=self.package_fixture(fixture)
                self.assertNotEqual(process.returncode, 0)
                self.assertIn(expected, process.stdout + process.stderr)
                self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
