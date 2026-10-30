# Evil Dead: Hail to the King — native engine recreation

[Русское описание](README.ru.md)

An unofficial C++17 engine recreation for modern Windows. The Dreamcast version
is the reference for gameplay, scripts, animation timing, cameras and sound.
Rendering uses **Vulkan through SDL3 GPU**. This is a work in progress, not an
official remaster or a console emulator.

**Source code only.** Game graphics, models, fonts, scripts, audio, movies, disc
images, saves and release packages are not included. The complete translation
catalog for the original game text is also excluded; only labels for the engine's
additional menus are included. Supply and prepare your own Dreamcast data locally.

## Features

- Execution of original JPC scripts and support for both chapters.
- Fixed cameras, collision, foreground occlusion, animation, combat, inventory,
  interactions, doors and saves.
- Keyboard and modern gamepads: remapping, dead zones and rumble.
- English/Russian UI support with the corresponding local data.
- Resolution selection, widescreen presentation preserving the original composition,
  and adjustable scanline and film effects.
- PNG images/masks, lossless FLAC music and AV1 movie playback with built-in decoders.

Windows x64 is the current target. Linux validation is deferred. Exact Dreamcast
rasterization is not claimed; visual and collision edge cases remain under review.

## Build on Windows

Requirements: Visual Studio with x64 C++ tools and Windows SDK, CMake 3.24+, Git,
Python 3.10+ with pip. Pillow is needed for image tools; FFmpeg/FFprobe are needed
only for offline media preparation. Dependency scripts fetch pinned SDL3 3.4.18
and dav1d 1.5.2 sources and verify SHA-256 hashes.

```powershell
.\scripts\build.ps1
```

Without game data, this builds the source and skips the full gameplay test run.
The executable is `build/episode/Release/evil-dead-scene.exe`; it cannot start the
game until a prepared asset profile is supplied. Shader source and eight generated
SPIR-V programs are included as engine code. No shader compiler is needed for a
normal build. The pinned dr_flac header retains its upstream license.

## Local game data and tests

`tools/extract_dreamcast.py` extracts your GDI image into a **new** directory.
Extract CHD locally with your own `chdman` first. The `export_dreamcast_*`,
`import_dreamcast_audio.py` and `import_dreamcast_runtime.py` tools convert prepared
components; see their `--help` contracts. There is currently no single-command
clean-disc-to-final-profile installer.

The default runtime profile is `build/episode/assets/`. The full suite also needs
the original 1x fixture at `build/dreamcast-upscale-native-source-20261028/assets/`.
Both are ignored by Git. The importer validates a specific recorded Dreamcast
extraction; arbitrary releases or modified profiles are not considered equivalent.
Do not modify sealed assets in place: local profiles may share immutable hardlinks.
Use new output directories for conversion, localization and upscaling.

For external profiles, after building dependencies:

```powershell
cmake -S . -B build/episode -A x64 `
  -DTEST_ASSETS="C:/private/game-assets" `
  -DNATIVE_FIXTURE_ASSETS="C:/private/original-fixture"
cmake --build build/episode --config Release
ctest --test-dir build/episode -C Release --output-on-failure
```

After building, this selection runs **without game assets**:

```powershell
ctest --test-dir build/episode -C Release --output-on-failure `
  -R "^(input|native-input-queue|ground|door-policy|jpc|source-floor-query|save-file)$"
python -B -m unittest discover -s tools -p test_archive_package.py
python -B -m unittest discover -s tools -p test_create_release_torrent.py
```

The full CTest suite and many Python exporter tests require local original data.
Those fixtures are not included in a source-only checkout.

## Layout and notices

- `runtime/` — engine, native compatibility code, shaders and C++ tests.
- `tools/` — offline conversion, inspection, localization and packaging tools.
- `scripts/` — pinned dependencies and Windows packaging.
- `localization/ui-ru.json` — source labels for additional engine menus.

[Windows packaging](README-package.md) operates on your own local game data.
Do not commit game data or attach playable packages to this repository's releases.
[Third-party notices](THIRD_PARTY_NOTICES.md) document retained dependency licenses.
No project-wide source license has been selected; third-party files keep their terms.
The original game's names and content remain with their respective rights holders.
