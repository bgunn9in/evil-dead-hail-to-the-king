import hashlib
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
import zipfile

from archive_package import ARCHIVE_ROOT, archive_package


class ArchivePackageTests(unittest.TestCase):
    def fixture(self, root):
        package = root / "package"
        rows = []
        for name, data in (("run.cmd", b"fixture; no executable is run"),
                           ("bin/evil-dead-scene.exe", b"unit fixture"),
                           ("assets/Русский текст.txt", b"original bytes\x00\x01")):
            path = package / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            rows.append({"file": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
        manifest = package / "package-manifest.json"
        manifest.write_text(json.dumps({"format": "EDPACKAGE1", "files": rows}), encoding="utf-8")
        return package, manifest, rows

    def test_round_trip_preserves_exact_members_and_bytes(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            package, _, _ = self.fixture(root)
            output = root / "release"
            report = archive_package(package, output)
            self.assertFalse((output / "INCOMPLETE.txt").exists())
            self.assertEqual(report["files"], 4)
            with zipfile.ZipFile(output / report["archive"]) as archive:
                self.assertIsNone(archive.testzip())
                for path in package.rglob("*"):
                    if path.is_file():
                        self.assertEqual(archive.read(ARCHIVE_ROOT + "/" + path.relative_to(package).as_posix()), path.read_bytes())
            first = (output / report["archive"]).read_bytes()
            self.assertEqual(hashlib.sha256(first).hexdigest(), report["sha256"])
            second = archive_package(package, root / "second")
            self.assertEqual(second["sha256"], report["sha256"])
            with self.assertRaises(FileExistsError):
                archive_package(package, output)
            self.assertEqual((output / report["archive"]).read_bytes(), first)

    def test_rejects_unsafe_incomplete_or_unlisted_files(self):
        for broken in ("../outside", "C:/outside", "assets\\outside", "assets//outside",
                       "case", "extra", "incomplete", "inside"):
            with self.subTest(broken=broken), TemporaryDirectory() as temporary:
                root = Path(temporary)
                package, manifest, rows = self.fixture(root)
                output = root / "release"
                if broken == "extra":
                    (package / "private-save.txt").write_text("must not be archived")
                elif broken == "incomplete":
                    (package / "INCOMPLETE.txt").write_text("unfinished")
                elif broken == "inside":
                    output = package / "release"
                else:
                    if broken == "case":
                        rows.append({**rows[0], "file": "RUN.CMD"})
                    else:
                        rows[0]["file"] = broken
                    manifest.write_text(json.dumps({"format": "EDPACKAGE1", "files": rows}))
                with self.assertRaises(ValueError):
                    archive_package(package, output)
                self.assertFalse(output.exists())

    def test_changed_bytes_leave_an_incomplete_marker(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            package, _, _ = self.fixture(root)
            (package / "bin/evil-dead-scene.exe").write_bytes(b"tampered")
            output = root / "release"
            with self.assertRaisesRegex(ValueError, "corrupt"):
                archive_package(package, output)
            self.assertTrue((output / "INCOMPLETE.txt").is_file())
            self.assertFalse((output / "SHA256SUMS.txt").exists())

    def test_runtime_zip_keeps_json_markers_resources_and_licenses_only(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            package, manifest, rows = self.fixture(root)
            extra = ("README-package.md", "assets/episode.json", "assets/levelB/episode.json",
                     "assets/provenance/offline.bin", "assets/actors/ash/source.json", "assets/movie.json",
                     "bin/shaders/scene.vert.spv", "bin/shaders/spirv-manifest.json", "licenses/SDL3-LICENSE.txt")
            for name in extra:
                data = b"fixture resource"
                path = package / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
                rows.append({"file":name, "bytes":len(data), "sha256":hashlib.sha256(data).hexdigest()})
            manifest.write_text(json.dumps({"format":"EDPACKAGE1", "files":rows}), encoding="utf-8")
            output = root / "release"
            report = archive_package(package, output, runtime_only=True)
            expected = {"bin/evil-dead-scene.exe", "assets/Русский текст.txt", "assets/episode.json",
                        "assets/levelB/episode.json", "bin/shaders/scene.vert.spv", "licenses/SDL3-LICENSE.txt", "NOTICE.txt"}
            with zipfile.ZipFile(output / report["archive"]) as archive:
                self.assertEqual({p.removeprefix(ARCHIVE_ROOT + "/") for p in archive.namelist()}, expected)
                for name in expected - {"NOTICE.txt"}:
                    self.assertEqual(archive.read(ARCHIVE_ROOT + "/" + name), (package / name).read_bytes())
            runtime = json.loads((output / "runtime-manifest.json").read_text())
            self.assertEqual({r["file"] for r in runtime["files"]}, expected)
            self.assertEqual(report["entrypoint"], "bin/evil-dead-scene.exe")
            self.assertNotIn("run.cmd", (output / "README.txt").read_text(encoding="utf-8"))
            self.assertIn("bin/evil-dead-scene.exe", (output / "README.txt").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
