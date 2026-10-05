# Wavy Studio

A small C++20 / Qt 6 Quick desktop skeleton. The engine plays silence; there
are no DAW features. xmake is the supported build system.

Requirements: xmake 3.1.1+, a C++20 compiler and Qt 6 (Quick and QuickControls2).
Dependencies (RtAudio 6.0.1, doctest) are fetched by xmake via `add_requires`.

## Linux

The default Qt SDK location is `/usr`.

RtAudio is built from source by xmake with ALSA, PulseAudio and JACK, so the
audio development headers must be installed first. The package names below are
from memory and not yet verified on every distro:

| Distro | Packages |
|---|---|
| Arch | `alsa-lib libpulse jack` (`pipewire-jack` also works) |
| Debian / Ubuntu | `libasound2-dev libpulse-dev libjack-jackd2-dev` |
| Fedora | `alsa-lib-devel pulseaudio-libs-devel jack-audio-connection-kit-devel` |

JACK is on by default on Linux; turn it off with `xmake f --jack=n`.

```sh
xmake f -m debug -y
xmake
xmake test
QT_QPA_PLATFORM=offscreen xmake run wavy-studio --smoke-test
xmake run wavy-studio
```

## Windows

Use a configured MSVC or MinGW compiler shell and a matching Qt 6 SDK. Replace
the example SDK location with your installed SDK directory.

```powershell
xmake f -m debug --qt="C:\Qt\6.11.2\msvc2022_64" -y
xmake
xmake test
$env:QT_QPA_PLATFORM = "offscreen"
xmake run wavy-studio --smoke-test
Remove-Item Env:QT_QPA_PLATFORM
xmake run wavy-studio
```

WASAPI and DirectSound are enabled on Windows. ASIO is optional: configure with
`--asio=y` (default `--asio=n`). ASIO is available under GPLv3, which matches
this project's license, so binaries may include it. Linux builds enable ALSA,
PulseAudio and JACK.

Keep Qt's `bin` directory on PATH for runtime DLLs. Distributions need Qt's
runtime deployment tools and the appropriate compiler runtime.

MinGW cross-build from Linux (untested; use a matching Windows MinGW Qt SDK):

```sh
xmake f -p mingw -a x86_64 --qt="<Windows Qt dir>" --mingw="<toolchain dir>"
xmake
```

## Continuous integration

GitHub Actions builds release binaries on Linux and Windows with xmake and
Qt 6.8.3, then runs the engine tests and an offscreen application smoke test.

## Logging

Logs go to stderr with a timestamp, level and category. `WAVY_LOG` sets the
minimum level: `trace`, `debug`, `info`, `warn` or `error` (default: `debug` in
debug builds, `info` in release). Colors are enabled for terminals;
`NO_COLOR` disables them. `WAVY_LOG_COLOR=always|never` overrides detection
and `NO_COLOR`. Logging is not suitable for the audio callback.

## Layout

- `xmake.lua`: static engine, Qt Quick application and doctest target.
- `src/engine/AudioEngine.*`: Qt-free engine and RtAudio device ownership.
- `src/app/EngineController.hpp`: QObject adapter exposed as `audioEngine`.
- `src/app/main.cpp`: app startup and smoke-test mode.
- `src/ui/main.qml`, `src/ui/ui.qrc`: dark placeholder UI embedded as resources.
- `tests/engine_test.cpp`: deterministic device-free lifecycle test.
- `src/ui/timeline/`: virtualized mock timeline; Ctrl+wheel zooms at the pointer, wheel/Shift+wheel scroll horizontally, and the right scrollbar scrolls tracks.
- `MockTimelineModel.stress`: generates 100 tracks with 200 clips each; the adapter contract is documented at the top of `TimelineView.qml`.

Engine control methods run on one control thread. Repeated start/stop calls
are safe. `sampleRate()` is zero while stopped and 48000 in no-device mode.
If audio initialization or startup fails, `start()` still succeeds and the
engine runs without a device. The test explicitly selects `DeviceMode::NoDevice`
so it never opens hardware. Smoke mode starts the default engine, loads the
actual QML and exits successfully after 100 ms; QML load failure returns 1.

Copyright (C) AzPepoze. Wavy Studio is licensed under the GNU General Public
License v3.0 or later (see `LICENSE`); dependency licenses apply separately.
