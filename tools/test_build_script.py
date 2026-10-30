"""Run the real build entry point with the SDL bootstrap and external tools mocked."""

import json
import os
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory
import unittest


ROOT = Path(__file__).resolve().parents[1]
POWERSHELL = shutil.which("powershell") or shutil.which("pwsh")


@unittest.skipUnless(POWERSHELL, "PowerShell is optional")
class BuildScriptTests(unittest.TestCase):
    def run_build(self, fail_at=0, native_status='verified-import'):
        with TemporaryDirectory() as temporary:
            path = Path(temporary)
            scripts = path / "scripts"
            scripts.mkdir()
            (scripts / "build.ps1").write_bytes((ROOT / "scripts/build.ps1").read_bytes())
            if native_status is not None:
                assets = path / 'build/episode/assets'
                assets.mkdir(parents=True)
                (assets / 'dreamcast-runtime.json').write_text(json.dumps({'format': 'ED-DREAMCAST-RUNTIME1', 'status': native_status}))
            (scripts / "build-sdl.ps1").write_text("Record-Tool 'sdl' @()\nif($LASTEXITCODE -ne 0){throw 'Forced SDL bootstrap failure'}\n", encoding="utf-8")
            (scripts / "build-dav1d.ps1").write_text("Record-Tool 'dav1d' @()\nif($LASTEXITCODE -ne 0){throw 'Forced AV1 bootstrap failure'}\n", encoding="utf-8")
            wrapper = path / "mock-build.ps1"
            wrapper.write_text("""
$global:stage=0
function Record-Tool($name,$arguments) {
    $global:stage++
    $line=@{name=$name;arguments=@($arguments)}|ConvertTo-Json -Compress
    [System.IO.File]::AppendAllText($env:ED_BUILD_LOG,$line+[Environment]::NewLine)
    $global:LASTEXITCODE=0
    if($global:stage -eq [int]$env:ED_BUILD_FAIL){$global:LASTEXITCODE=17}
}
function global:python {Record-Tool 'python' $args}
function global:cmake {Record-Tool 'cmake' $args}
function global:ctest {Record-Tool 'ctest' $args}
& (Join-Path $env:ED_BUILD_ROOT 'scripts/build.ps1')
exit $LASTEXITCODE
""", encoding="utf-8")
            env = {**{k: v for k, v in os.environ.items() if k.lower() != "psmodulepath"},
                   "ED_BUILD_ROOT": str(path), "ED_BUILD_LOG": str(path / "calls.jsonl"),
                   "ED_BUILD_FAIL": str(fail_at)}
            process = subprocess.run([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(wrapper)],
                                     capture_output=True, text=True, env=env, timeout=30)
            log = path / "calls.jsonl"
            rows = [json.loads(line) for line in log.read_text().splitlines()] if log.is_file() else []
            return process, rows, path

    def test_sealed_dreamcast_build_does_not_export_pc_resources(self):
        process, rows, project = self.run_build()
        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
        self.assertEqual([r["name"] for r in rows], ["sdl", "dav1d", "cmake", "cmake", "ctest"])
        self.assertEqual(rows[2]["arguments"], ["-S", str(project), "-B", str(project / "build/episode"), "-A", "x64"])

    def test_missing_or_unverified_native_profile_stops_before_tools(self):
        for status in (None, 'INCOMPLETE'):
            with self.subTest(status=status):
                process, rows, _ = self.run_build(native_status=status)
                self.assertNotEqual(process.returncode, 0)
                self.assertEqual(rows, [])

    def test_every_failed_stage_stops_the_pipeline(self):
        for stage in range(1, 6):
            process, rows, _ = self.run_build(stage)
            self.assertEqual(process.returncode, 1 if stage in (1,2) else 17, process.stdout + process.stderr)
            self.assertEqual(len(rows), stage)


if __name__ == "__main__":
    unittest.main()
