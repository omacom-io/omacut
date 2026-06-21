# Omacut

A dead-simple video **length** trimmer. Open a video, drag the two yellow handles to pick a start and end, preview the clip, and export.

Built using **Qt Quick (QML)** UI with the Material style — the same Qt stack Quickshell builds on — and **ffmpeg** for the cut. The C++ side compiles to a single executable; the QML is embedded in it via Qt resources.

## Hotkeys

- *Escape*: Open a new file to trim.
- *Space*: Start/stop video playback.
- *Return*: Export the current trim.
- *Keypad Enter*: Export the current trim.

## Install

Install via the Omarchy Package Repository via the `omacut` package. It's installed by default in new installations of Omarchy (from Quattro forward).

## Requirements

- `xdg-desktop-portal` and a portal backend for the file picker
- `ffmpeg` and `ffprobe` on your PATH (used at runtime)

Exports are always written as MP4 files, regardless of the input video's container.

## Build

Uses Qt's own build tool, `qmake6` (no cmake needed):

```bash
./bin/build
```

This produces a single `omacut` binary in `build/`.

Requirements:

- A C++17 compiler and Qt6: `qt6-base`, `qt6-declarative` (Qt Quick + Controls),
  `qt6-multimedia`

## Test

```bash
./bin/test
```

## Package

Build and install the local Arch package:

```bash
./bin/install
```

This runs `./bin/build`, then `makepkg -fsi` from `pkgbuild/` so same-version local packages are rebuilt and reinstalled. Extra arguments are passed through to `makepkg`, for example `./bin/install --clean`. The package installs the binary, desktop entry, app icon, and MIT license. Local package outputs such as `pkgbuild/pkg/`, `pkgbuild/src/`, and `*.pkg.tar.*` are ignored.

## License

MIT. See `LICENSE`.
