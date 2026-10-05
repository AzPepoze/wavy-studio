# Engine

Qt-free C++ engine, built as the `wavy_engine` static library.

- `core/`: logging.
- `audio/`: audio device ownership and the real-time path.
- `io/`: audio decoding and waveform peak data.
- `timeline/`: timeline data model, edit commands and undo history.

`src/engine` is the include root, so headers are included as
`"<folder>/<Name>.hpp"` (for example `"timeline/Timeline.hpp"`). New
subsystems get their own folder.
