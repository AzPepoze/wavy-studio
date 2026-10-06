# Engine

Qt-free C++ engine, built as the `wavy_engine` static library.

- `core/`: logging.
- `audio/`: audio device ownership and the real-time path.
- `io/`: audio decoding and waveform peak data.
- `timeline/`: timeline data model, edit commands and undo history.
- `timeline/CompoundCommand.hpp/.cpp`: ordered edits validated on a scratch timeline with cloned commands.
- `timeline/ClipCommands.cpp`: clip edits, including composite `EditClip` move and trim.
- `record/`: device-free audio capture and durable WAV streaming.
- `record/RingBuffer.hpp`: preallocated SPSC sample queue and overflow accounting.
- `record/WavWriter.hpp`: streaming float WAV writer API.
- `record/WavWriter.cpp`: exclusive file creation, durable header patches and RIFF size limit.
- `record/Recorder.hpp`: take metadata, capture API and atomic meters.
- `record/Recorder.cpp`: punch-in, writer thread and continuous-prefix recovery.
- `record/CommitTake.hpp`: recorded-take commit API.
- `record/CommitTake.cpp`: validated AddClip through undo history.

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

Recording control runs on the engine control thread: start the engine, select a
fresh path with `recorder().arm(path, channels)`, call `startAtFrame(frame)`, and
play the transport. `stop()` closes the writer and returns a take for
`record::commitTake`; it never deletes the audio file. Existing files are not
overwritten. Input selection requires a stopped engine; missing input or duplex
failure preserves output playback. Monitor gain defaults to zero and monitoring
also requires the atomic enable toggle. `feedInputOffline` uses the device
callback's capture, monitor and transport path without opening hardware.

The SPSC queue publishes samples with release/acquire ordering, keeps indices on
separate cache lines, and never waits in capture. Stop closes capture admission,
waits for an in-flight capture on the control thread, then drains and joins the
writer. Overflow, missing input or a transport discontinuity retains the
continuous prefix and counts subsequent dropped frames; it does not shorten a
gap into apparently continuous audio. Meter decay is applied as input arrives.

RtAudio 6 exposes only a combined duplex latency. `inputLatencyFrames()` reports
that round-trip alignment delay, `outputLatencyFrames()` is zero for duplex and
reports the output latency for output-only streams. The recorder subtracts the
rounded alignment delay from the first captured timeline frame, clamped to zero.
These values are not separately measured directional latencies.

WAV headers are flushed and synced at creation, each MiB of data, at least once
per second while recording, and at close. A killed process leaves the last
patched prefix playable; queued or unpatched samples are not guaranteed durable.
RIFF recording stops with an explicit error before the 4 GiB limit. A power
failure during a header write is outside the process-crash guarantee.
