# Wnamp Style Player

A native Windows desktop music player, written in C++, that loads classic **Winamp 2** skins
(`.wsz`). No Qt, no web tech, no Electron — just Win32, GDI and GStreamer.

![icon](resources/app.ico)

This is a personal/hobby project. It is **not affiliated with, endorsed by, or associated with
Winamp or Nullsoft**. It reads the public Winamp 2 skin format; no Winamp/Nullsoft code or
assets are included. [Webamp](https://github.com/captbaritone/webamp) (MIT) and
[XMMS Resuscitated](https://gitlab.com/cschalle/xmms-resuscitated) (GPL) were used only as
references for sprite layout/behavior — no code was copied from either.

## Features

- Loads and renders classic Winamp 2 `.wsz` skins at an integer scale, per-pixel-alpha shaped
  window (via `region.txt`), DPI-aware
- Plays anything GStreamer can decode (MP3, FLAC, OGG, WAV, AAC, …), with gapless playback
  between tracks
- Playlist window: add/remove, save/load `.m3u`/`.m3u8`/`.pls`, multi-select, and drag-and-drop
  reordering
- 10-band equalizer window with presets and a response curve
- Spectrum analyzer and position/volume/balance sliders synced to actual playback
- Internet radio: built-in stations (Jumbo Radio, Tempo FM), a SomaFM channel browser,
  and any direct stream URL (paste one from the clipboard)
- Skin browser for switching skins without restarting
- Persistent settings (last skin, volume, EQ, window position) in `%APPDATA%\wnamp`

## Build

Requires Visual Studio 2022 (MSVC), CMake, and the
[official GStreamer MSVC development + runtime installers](https://gstreamer.freedesktop.org/download/)
(x86_64, matching your build arch).

```powershell
.\build.ps1          # configures + builds build\Release\wnamp.exe
.\run.ps1             # runs it with the default skin
.\make_shortcut.ps1   # creates a Desktop shortcut (after a build)
```

`build.ps1` looks for the GStreamer SDK via the `GSTREAMER_1_0_ROOT_MSVC_X86_64` environment
variable, a `-GStreamerRoot` argument, or `local.config.ps1` (see `local.config.ps1` — gitignored,
machine-specific paths). Without a GStreamer SDK it still builds with `-DWNAMP_GSTREAMER=OFF`,
using a no-op audio engine (skins load and the UI works, but nothing plays).

miniz is fetched automatically via CMake `FetchContent` on first configure (needs network).

## Layout

- `src/audio/` — the `IEngine` interface plus the GStreamer and null engine implementations.
  This is the **only** part of the codebase that includes GStreamer headers; the UI talks to
  engines only through `IEngine`.
- `src/skin/` — `.wsz` loader (miniz + WIC), `region.txt` parser, sprite rectangles for every
  skin window.
- `src/ui/` — the layered main window, equalizer, playlist, and skin browser windows.
- `src/playlist/` — playlist model and `.m3u`/`.m3u8`/`.pls` I/O.
- `src/net/` — a small WinHTTP client and the SomaFM channel-list/stream-resolution code.
- `src/radio/` — built-in stations (`Stations.h`) and the "Radio..." popup menu (`RadioMenu`).
- `src/settings/` — persistent settings.
- `tools/make_default_skin.py` — generates the original default skin shipped in `data/`.

## License

MIT — see [LICENSE](LICENSE).
