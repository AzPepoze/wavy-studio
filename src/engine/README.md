# Engine

Qt-free C++ engine, built as the `wavy_engine` static library.

- `core/`: logging.
- `audio/`: audio device ownership and the real-time path.
- `io/`: audio decoding and waveform peak data.
- `timeline/`: timeline data model, edit commands and undo history.
- `timeline/CompoundCommand.hpp/.cpp`: ordered edits validated on a scratch timeline with cloned commands.
- `timeline/ClipCommands.cpp`: clip edits, including composite `EditClip` move and trim.

`src/engine` is the include root, so headers are included as
`"<folder>/<Name>.hpp"` (for example `"timeline/Timeline.hpp"`). New
subsystems get their own folder.

- `effects/`: prepared stereo DSP with immutable schemas and relaxed atomic live parameters.
- `effects/Parameter.hpp`: fixed parameter blocks and five-millisecond linear smoothing.
- `effects/Effect.hpp`: effect interface, slot serialization and main-thread registry.
- `effects/Effect.cpp`: built-in registration and shared schema validation.
- `effects/GainPan.hpp`: constant-power gain/pan interface (center is -3.01 dB per channel).
- `effects/GainPan.cpp`: smoothed gain, pan and polarity processing.
- `effects/ParametricEq.hpp`: four stereo biquad bands with independent double state.
- `effects/ParametricEq.cpp`: RBJ shelves/peaks, coefficient ramps and denormal suppression.
- `effects/Compressor.hpp`: linked peak/RMS compressor with atomic reduction meter.
- `effects/Compressor.cpp`: soft knee, envelope time constants and parallel/auto makeup gains.

`EffectFactory::slot()` creates the parameter owner. Keep slots in an `EffectChains`
configuration keyed by `timeline::TrackId`, with a separate master vector, and pass
it to `buildSnapshot()`. This keeps effect configuration independent of timeline
command history. Snapshot construction creates independent DSP instances sharing
those parameter owners, validates schemas, prepares DSP at the timeline rate, and
allocates track scratch buffers. Large render requests are split to the configured
capacity (default 512 frames). Empty and fully bypassed track chains retain the
original direct summation path. Chain structure and bypass edits require snapshot
publication; parameter values and the reduction meter use relaxed atomics and can
be edited/read live. Multiple parameter writes are independent, not transactional.
Only the render thread may process/reset prepared DSP; reset/prepare otherwise
require rendering stopped. RMS envelopes smooth stereo mean-square power; peak
envelopes smooth the larger channel magnitude. Attack/release use e-folding time
constants. Auto makeup compensates the static compression at 0 dBFS.
