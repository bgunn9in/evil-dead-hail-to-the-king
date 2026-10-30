# Third-party notices / Сторонние компоненты

Original game content is not included. This repository does not grant rights to
the original game's graphics, fonts, models, text, audio, scripts or movies.
No project-wide source license has been selected.

Оригинальный игровой контент не включён. Репозиторий не предоставляет прав на
исходные графику, шрифты, модели, текст, звук, сценарии или ролики игры.
Общая лицензия на исходники проекта пока не выбрана.

| Component | Use | License / notice |
| --- | --- | --- |
| dr_flac | Static FLAC decoder | Upstream header, license and exact revision in `runtime/third_party/`. Public domain or MIT No Attribution, as stated by upstream. |
| SDL3 3.4.18 | Window, Vulkan GPU, audio and input | zlib license. Downloaded by `scripts/build-sdl.ps1`; its license is retained in generated packages. |
| dav1d 1.5.2 | Static AV1 decoder | BSD-2-Clause. Downloaded by `scripts/build-dav1d.ps1`; its COPYING notice is retained in generated packages. |
| Microsoft Visual C++ runtime | Windows app-local DLLs | Not included here. Local packaging uses the installed Visual Studio redistributable tree and records its notices. |
| FFmpeg / FFprobe | Optional offline media tools | External tools, not included or linked. Use the terms of your installed build. |

Dependency scripts pin source archive hashes. Keep upstream copyright notices and
license files when redistributing the corresponding components.
Скрипты закрепляют хеши архивов зависимостей; уведомления авторов и тексты лицензий
соответствующих компонентов сохраняются при их распространении.
