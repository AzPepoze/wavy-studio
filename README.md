# Wavy Studio

A C++23 / Qt 6 Quick audio workstation with an editable timeline, stereo
playback, per-track effects and audio recording. The engine is Qt-free.
xmake is the supported build system.

Requirements: xmake 3.1.1+, a C++23 compiler and Qt 6 (Quick and QuickControls2).
The audio loading API uses `std::expected`. A target sample rate of zero
keeps the native rate. Peak queries use whole pyramid buckets, conservatively
including boundary samples at the selected resolution.
Dependencies (RtAudio 6.0.1, doctest, miniaudio, nlohmann_json) are fetched by xmake via `add_requires`.

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

Run the headless QML timeline tests with `xmake test "ui_tests/*"` (requires Qt’s `qmltestrunner`).

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

- `src/engine/audio/Mixer.hpp`: mixer API.
- `src/engine/audio/Mixer.cpp`: real-time stereo rendering.
- `src/engine/audio/Snapshot.hpp`: snapshot data model.
- `src/engine/audio/Snapshot.cpp`: source preparation.
- `src/engine/audio/Transport.hpp`: atomic transport state.
- `src/engine/audio/Transport.cpp`: loop control.
- `tests/audio/mixer_test.cpp`: mixer accuracy, allocation, concurrency and performance tests.

- `xmake.lua`: static engine, Qt Quick application and doctest target.
- `src/engine/core/`: logging.
- `src/engine/audio/`: audio device ownership and the real-time path.
- `src/engine/io/`: audio decoding and waveform peak data.
- `src/engine/io/SourceLibrary.hpp`: asynchronous source cache API and immutable audio/peak ownership.
- `src/engine/io/SourceLibrary.cpp`: worker pool, generated waveforms and memory-budgeted LRU eviction.
- `tests/io/source_library_test.cpp`: source loading, concurrency, generation, eviction and throughput tests.
- `src/engine/timeline/`: timeline data model, edit commands and undo history.
- `src/engine/timeline/CompoundCommand.hpp/.cpp`: ordered edits validated on a scratch timeline with cloned commands.
- `src/engine/timeline/ClipCommands.cpp`: clip edits, including composite `EditClip` move and trim.
- `src/engine/effects/`: Qt-free gain/pan, four-band RBJ EQ, linked peak/RMS compressor, Freeverb reverb, tempo-synced stereo delay, mid/side stereo width, look-ahead limiter and split/wide-band de-esser.
- `tests/effects/`: device-free effects test suite.
- `tests/effects/effects_test.cpp`: DSP response, timing, live parameters, allocation and 64-track benchmarks.
- `tests/effects/fx2_test.cpp`: reverb RT60/bounds, delay timing/glide, stereo-width mid/side, allocation, threading and 64-track benchmarks.
- `tests/effects/fx3_test.cpp`: limiter ceiling and look-ahead, de-esser ratio/mode, allocation and 64-track benchmark tests.
- `src/engine/record/`: device-free audio capture and durable WAV streaming.
- `src/engine/record/RingBuffer.hpp`: preallocated SPSC sample queue and overflow accounting.
- `src/engine/record/WavWriter.hpp`: streaming float WAV writer API.
- `src/engine/record/WavWriter.cpp`: exclusive file creation, durable header patches and RIFF size limit.
- `src/engine/record/Recorder.hpp`: take metadata, capture API and atomic meters.
- `src/engine/record/Recorder.cpp`: punch-in, writer thread and continuous-prefix recovery.
- `src/engine/record/CommitTake.hpp`: recorded-take commit API.
- `src/engine/record/CommitTake.cpp`: validated AddClip through undo history.
- `tests/record/`: deterministic recording and disk throughput tests.
- `tests/record/record_test.cpp`: SPSC concurrency, WAV recovery, punch-in, monitoring and allocation checks.
- `src/engine/project/Project.hpp`: project save/load API, metadata and error codes.
- `src/engine/project/Project.cpp`: atomic JSON writer and validating reader for the timeline.
- `tests/project/`: round-trip, source-path, validation, atomic-save and performance tests.
- `tests/audio/`: deterministic device-free lifecycle test.
- `tests/io/`: generated audio and peak tests.
- `tests/timeline/`: command, history, stress and range-query tests.
- `src/app/EngineController.hpp`: QObject adapter exposed as `audioEngine`.
- `src/app/main.cpp`: app startup and smoke-test mode.
- `src/ui/main.qml`, `src/ui/ui.qrc`: dark timeline UI embedded as resources.
- `src/ui/timeline/`: virtualized timeline; Ctrl+wheel zooms at the pointer, wheel/Shift+wheel scroll horizontally, and the right scrollbar scrolls tracks.
- `MockTimelineModel.stress`: generates 100 tracks with 200 clips each; the adapter contract is documented at the top of `TimelineView.qml`.

Timeline editing, history and queries run on the main thread. Range queries use
half-open intervals and include overlapping clips. Timeline equality compares
visible state; ID allocation counters survive undo to prevent ID reuse.
Validation returns a bool-like `Result` with an `Error` enum.

Play or Space toggles playback/pause; Stop returns to frame zero. Clicking or
dragging the ruler seeks, and the playhead follows the transport at about 60 Hz.
The status bar shows playback state, time, sample rate and device session state.
Audio hardware opens lazily on first Play and stays open while paused or stopped.
A device initialization failure leaves a silent no-device session; real hardware
availability is not implied by an open session. Smoke mode loads the actual QML
and exits after 100 ms without opening audio hardware.

`SnapshotPublisher` coalesces timeline edits, undo/redo and track state changes
at 20 ms intervals. It decodes sources on a worker and publishes immutable mixer
snapshots on the control thread, which also collects retired snapshots. Pending
and missing sources are silent. Sources are cached for the publisher's lifetime.
The demo uses `generated:sine:<Hz>:0.2`; `saw`, `square` and deterministic `noise`
are also supported, with an optional amplitude (default 0.2). Generated audio
covers each source's required clip length and offset, and grows after longer edits.
`EngineController` exposes play, pause, togglePlay, stop, seek and loop controls.
App tests use `DeviceMode::NoDevice` and `renderOffline` to verify the same mixer
path without hardware.

Effect chains are prepared through `EffectChains` track-ID mappings and a master
chain passed to `buildSnapshot`; slot parameter blocks are shared with the audio
thread for live atomic edits. See `src/engine/README.md` for the control/render
contract. Run the device-free tests with `xmake test effects_tests`.

Arm a track with its circle button, choose an input and mono/stereo channels in
Audio Settings, then press Record or R to punch in at the playhead. R is disabled
while typing. Stop or R finishes the take and adds a latency-compensated clip in
one undo step; the playhead remains at its end. Looping and transport seeking are
disabled during recording. The input meter and elapsed time update during capture.
Optional monitoring includes device latency. Takes use unique, Windows-safe names
under `<Documents>/Wavy Studio/Recordings`; undo keeps the WAV on disk. Dropped
frames produce a status warning while preserving the recorded prefix.

The recording API, latency convention and crash recovery limits are documented
in [the engine README](src/engine/README.md).

## Contributing

Commit messages follow [Conventional Commits](https://www.conventionalcommits.org):
`type(scope): lowercase imperative summary`, with type one of `feat`, `fix`,
`perf`, `refactor`, `docs`, `test`, `build`, `ci`, `chore`, `style`, `revert`
(for example `feat(timeline): add clip split`). Enable the hook that checks this
once per clone:

```sh
git config core.hooksPath .githooks
```

Copyright (C) AzPepoze. Wavy Studio is licensed under the GNU General Public
License v3.0 or later (see `LICENSE`); dependency licenses apply separately.
