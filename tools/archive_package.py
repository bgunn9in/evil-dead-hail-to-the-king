"""Archive an existing verified portable package without changing its contents."""
import argparse
import hashlib
from io import BytesIO
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import zipfile


ARCHIVE_ROOT = "EvilDead-HailToTheKing-Windows-x64"
RUNTIME_NOTICE = b"""Evil Dead: Hail to the King - unofficial modern-engine prototype
Original content platform: Sega Dreamcast.
Original backgrounds, models, fonts, scripts, sounds and movies retain
their original copyright. No ownership of the original content is claimed.
Russian text and modern presentation are derived additions.

Third-party notices are retained in licenses/:
SDL3-LICENSE.txt - SDL3
dav1d-COPYING.txt - statically linked AV1 decoder
dr_flac-LICENSE.txt - statically linked FLAC decoder
MSVC-runtime.txt - app-local Microsoft Visual C++ libraries
"""


def runtime_member(name):
    path = PurePosixPath(name)
    if name.startswith("assets/"):
        # shortcut: episode.json is the only runtime JSON marker; extend when adding JSON loaders.
        return "provenance" not in path.parts and (path.suffix != ".json" or path.name == "episode.json")
    return (name.startswith("bin/") and path.suffix in (".exe", ".dll", ".spv") or
            name.startswith("licenses/"))


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def archive_package(package, output, runtime_only=False):
    package, output = Path(package).resolve(), Path(output).resolve()
    if output.is_relative_to(package):
        raise ValueError("Archive output must be outside the package")
    if output.exists():
        raise FileExistsError("Choose a NEW output directory")
    manifest_bytes = (package / "package-manifest.json").read_bytes()
    manifest = json.loads(manifest_bytes)
    if manifest.get("format") != "EDPACKAGE1" or (package / "INCOMPLETE.txt").exists():
        raise ValueError("A complete EDPACKAGE1 package is required")
    rows, names = [], set()
    for row in manifest["files"]:
        name = row["file"]
        path = PurePosixPath(name)
        if (not name or path.is_absolute() or str(path) != name or
                any(part in (".", "..") for part in path.parts) or
                any(char in name for char in "\\:\x00\r\n") or name.casefold() in names or
                name.casefold() == "package-manifest.json"):
            raise ValueError("Unsafe or duplicate package member")
        if (type(row["bytes"]) is not int or row["bytes"] < 0 or
                not re.fullmatch("[0-9a-f]{64}", row["sha256"])):
            raise ValueError("Invalid package size/hash")
        names.add(name.casefold())
        rows.append(row)
    expected = {row["file"] for row in rows} | {"package-manifest.json"}
    actual = set()
    for directory, directories, files in os.walk(package, followlinks=False):
        for name in directories + files:
            path = Path(directory) / name
            info = path.lstat()
            if (stat.S_ISLNK(info.st_mode) or
                    getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT):
                raise ValueError("Linked/reparse package entries are not accepted")
            if name in files:
                if not stat.S_ISREG(info.st_mode):
                    raise ValueError("Non-file package entry")
                actual.add(path.relative_to(package).as_posix())
    if actual != expected or not {"run.cmd", "bin/evil-dead-scene.exe"} <= expected:
        raise ValueError("Package files differ from the manifest")
    manifest_hash = hashlib.sha256(manifest_bytes).hexdigest()
    rows.append({"file": "package-manifest.json", "bytes": len(manifest_bytes), "sha256": manifest_hash})
    excluded = []
    if runtime_only:
        excluded = [row["file"] for row in rows if not runtime_member(row["file"])]
        rows = [row for row in rows if runtime_member(row["file"])]
        rows.append({"file": "NOTICE.txt", "bytes": len(RUNTIME_NOTICE), "sha256": hashlib.sha256(RUNTIME_NOTICE).hexdigest()})
    output.mkdir(parents=True, exist_ok=False)
    marker = output / "INCOMPLETE.txt"
    marker.write_text("Archive is not yet verified. Do not publish this directory.\n", encoding="utf-8")
    archive = output / (ARCHIVE_ROOT + ".zip")
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as target:
        for index, row in enumerate(sorted(rows, key=lambda row: row["file"])):
            source = package / row["file"]
            if not source.resolve().is_relative_to(package):
                raise ValueError("Package member escaped its root")
            checksum, size = hashlib.sha256(), 0
            notice = runtime_only and row["file"] == "NOTICE.txt"
            with (BytesIO(RUNTIME_NOTICE) if notice else source.open("rb")) as stream, target.open(ARCHIVE_ROOT + "/" + row["file"], "w",
                    force_zip64=row["bytes"] >= zipfile.ZIP64_LIMIT) as member:
                while chunk := stream.read(1024 * 1024):
                    checksum.update(chunk)
                    size += len(chunk)
                    member.write(chunk)
            if size != row["bytes"] or checksum.hexdigest() != row["sha256"]:
                raise ValueError("Package changed or is corrupt: " + row["file"])
            if index % 500 == 0:
                print(f"Archived {index + 1}/{len(rows)}", flush=True)
    with zipfile.ZipFile(archive) as check:
        if len(check.infolist()) != len(rows):
            raise ValueError("Archive member count differs")
        for index, row in enumerate(rows):
            with check.open(ARCHIVE_ROOT + "/" + row["file"]) as member:
                if (check.getinfo(member.name).file_size != row["bytes"] or
                        hashlib.file_digest(member, "sha256").hexdigest() != row["sha256"]):
                    raise ValueError("Archive verification failed: " + row["file"])
            if index % 2000 == 0:
                print(f"Verified {index + 1}/{len(rows)}", flush=True)
    report = {"format": "ED-PUBLICATION-ZIP1", "archive": archive.name,
              "sha256": digest(archive), "archive_bytes": archive.stat().st_size,
              "unpacked_bytes": sum(row["bytes"] for row in rows), "files": len(rows),
              "archive_root": ARCHIVE_ROOT, "package_manifest_sha256": manifest_hash,
              "compression": "ZIP Deflate level6", "runtime_files_changed": 0}
    if runtime_only:
        report.update(profile="runtime-only", entrypoint="bin/evil-dead-scene.exe", excluded_files=excluded,
                      notice="Concise copyright notice; all third-party license files retained.")
        (output / "runtime-manifest.json").write_text(json.dumps({"format":"ED-RUNTIME-ZIP1", "files":rows}, indent=2) + "\n", encoding="utf-8")
    (output / "release.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (output / "SHA256SUMS.txt").write_text(report["sha256"] + "  " + archive.name + "\n", encoding="ascii")
    (output / "README.txt").write_text(
        "Evil Dead: Hail to the King — Windows x64 / Vulkan\n\n" +
        ("Распакуйте ZIP целиком и запустите bin/evil-dead-scene.exe.\n" if runtime_only else
         "Распакуйте ZIP целиком в отдельную папку и запустите run.cmd.\n") +
        "Не запускайте игру непосредственно из окна архива.\n"
        "Требуется Windows 10/11 x64 и видеодрайвер с поддержкой Vulkan.\n"
        "Python, Visual Studio и отдельные кодеки для игры не требуются.\n"
        "Настройки и сохранения находятся в профиле Windows пользователя.\n" +
        ("Внутри архива только файлы запуска, игровые ресурсы и обязательные лицензии.\n" if runtime_only else
         "Подробное управление и ограничения: README-package.md внутри игры.\n") +
        "Исходные права и лицензии сохранены в NOTICE.txt и licenses/.\n\n"
        f"Архив: {report['archive_bytes']:,} байт. После распаковки: {report['unpacked_bytes']:,} байт.\n"
        "SHA256SUMS.txt содержит контрольную сумму ZIP.\n\n" +
        ("Extract the complete ZIP, then run bin/evil-dead-scene.exe.\n" if runtime_only else
         "Extract the complete ZIP to a separate folder, then run run.cmd.\n") +
        "Windows 10/11 x64 and a Vulkan-capable graphics driver are required.\n" +
        ("Copyright and licenses: NOTICE.txt and licenses/.\n" if runtime_only else
         "Read README-package.md, NOTICE.txt and licenses/ inside the game folder.\n"),
        encoding="utf-8")
    marker.unlink()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runtime-only", action="store_true", help="Omit developer documentation and offline provenance; preserve runtime assets and licenses")
    args = parser.parse_args()
    print(json.dumps(archive_package(args.package, args.output, args.runtime_only)), flush=True)


if __name__ == "__main__":
    main()
