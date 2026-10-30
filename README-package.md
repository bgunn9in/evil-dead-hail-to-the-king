# Local Windows packages / Локальные пакеты Windows

Game assets and playable packages are excluded from this source repository.
These commands use your own already prepared sealed Dreamcast data in
`build/episode/assets/` and a local `Release` build:

```powershell
.\scripts\package.ps1 -OutputDirectory .\dist\my-local-package
python -B tools/archive_package.py --package dist/my-local-package --output dist/my-local-archive
```

Use a **new** output directory each time. The packager checks the asset seal,
dependency versions and all file hashes. `INCOMPLETE.txt` means verification did
not finish. Never put output inside the original data or build tree.
Select another build with `package.ps1 -BuildDirectory .\build\my-build`.

`compress_dreamcast_runtime.py --help` describes a fresh lossless PNG/FLAC profile.
Preparation tools need Pillow and FFmpeg; the prepared runtime does not.
`archive_package.py --runtime-only` keeps runtime files and licenses and writes
instructions/checksums next to the ZIP. Extract completely, then run
`bin/evil-dead-scene.exe`. The full package also has `run.cmd`.
Windows 10/11 x64 and a compatible Vulkan driver are required.

Do not commit these packages or attach them to this repository's releases:
they contain your local game data. Sealed files may be hardlinked and must not
be edited in place. Use new output profiles for conversions.

---

Ресурсы и готовые игровые пакеты исключены из репозитория исходников. Команды выше
используют собственный проверенный Dreamcast-профиль обеих глав в
`build/episode/assets/` и локальную сборку `Release`.

Каждый раз выбирайте **новый** каталог назначения. Упаковщик сверяет профиль,
версии зависимостей и все хеши. `INCOMPLETE.txt` означает незавершённую проверку.
Внутрь исходных данных или дерева сборки результат помещать нельзя.
Другую сборку выбирают параметром `-BuildDirectory` скрипта `package.ps1`.

`compress_dreamcast_runtime.py` создаёт отдельный PNG/FLAC-профиль без потерь.
Инструментам подготовки нужны Pillow и FFmpeg; игроку они не требуются.
Режим архива `--runtime-only` оставляет файлы игры и лицензии, а инструкции и
контрольные суммы пишет рядом с ZIP. После полной распаковки запускать
`bin/evil-dead-scene.exe`; полный пакет также содержит `run.cmd`.
Нужны Windows 10/11 x64 и совместимый драйвер Vulkan.

Пакеты содержат локальные игровые данные: их не коммитят и не прикладывают к
релизам исходников. Не изменяйте запечатанные ресурсы на месте; профили могут
разделять файлы через жёсткие ссылки. Преобразования выполняются в новые каталоги.
