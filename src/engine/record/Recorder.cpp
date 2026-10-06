#include "record/Recorder.hpp"
#include "core/Log.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace wavy::record {
Recorder::Recorder(unsigned rate, std::size_t capacity) : rate_(rate), capacity_(capacity) {}
Recorder::~Recorder() { stop(); }
bool Recorder::setSampleRate(unsigned rate) {
    if (!rate || (thread_.joinable() && rate != rate_))
        return false;
    rate_ = rate;
    return true;
}
bool Recorder::arm(const std::filesystem::path& path, unsigned channels) {
    stop();
    error_.clear();
    if (!writer_.open(path, channels, rate_)) {
        error_ = writer_.error();
        log::error("record", "{}", error_);
        return false;
    }
    ring_ =
        std::make_unique<RingBuffer>(capacity_ ? capacity_ : std::size_t(rate_) * 10 * 2, channels);
    path_ = path;
    channels_.store(channels, std::memory_order_relaxed);
    start_.store(-1, std::memory_order_relaxed);
    dropped_.store(0, std::memory_order_relaxed);
    accepted_.store(0, std::memory_order_relaxed);
    failed_.store(false, std::memory_order_relaxed);
    nextPosition_ = -1;
    overrun_ = false;
    for (auto& peak : peaks_)
        peak.store(0, std::memory_order_relaxed);
    thread_ = std::jthread([this](std::stop_token token) { drain(token); });
    return true;
}
bool Recorder::startAtFrame(std::int64_t frame) {
    if (!thread_.joinable() || gate_.load(std::memory_order_acquire) || frame < 0)
        return false;
    punch_ = frame;
    gate_.store(1, std::memory_order_release);
    return true;
}
void Recorder::setInputLatencyFrames(double frames) {
    latency_ = std::isfinite(frames)
                   ? std::clamp(frames, 0.0, double(std::numeric_limits<std::int64_t>::max() / 2))
                   : 0;
}
float Recorder::peak(unsigned channel) const noexcept {
    return channel < 2 ? peaks_[channel].load(std::memory_order_relaxed) : 0;
}
void Recorder::capture(const float* input, std::size_t frames, std::int64_t position) noexcept {
    unsigned expected = 1;
    if (!gate_.compare_exchange_strong(expected, 3, std::memory_order_acquire))
        return;
    const auto channels = channels_.load(std::memory_order_relaxed);
    if (position >= 0 &&
        frames <= std::size_t(std::numeric_limits<std::int64_t>::max() - position)) {
        const auto skip =
            position < punch_ ? std::min(frames, static_cast<std::size_t>(punch_ - position)) : 0;
        if (input)
            input += skip * channels;
        frames -= skip;
        position += static_cast<std::int64_t>(skip);
        if (frames) {
            for (unsigned channel = 0; input && channel < channels; ++channel) {
                float level = peaks_[channel].load(std::memory_order_relaxed) *
                              static_cast<float>(std::exp(-double(frames) / (rate_ * 0.3)));
                for (std::size_t frame = 0; frame < frames; ++frame)
                    level = std::max(level, std::abs(input[frame * channels + channel]));
                peaks_[channel].store(level, std::memory_order_relaxed);
            }
            if (start_.load(std::memory_order_relaxed) < 0)
                start_.store(position, std::memory_order_relaxed);
            // Keep a continuous prefix on overflow or transport jumps; never splice a gap out of
            // time.
            if (!input || (nextPosition_ >= 0 && position != nextPosition_))
                overrun_ = true;
            const auto accepted = overrun_ || failed_.load(std::memory_order_relaxed)
                                      ? 0
                                      : ring_->tryWrite(input, frames * channels) / channels;
            if (accepted != frames)
                overrun_ = true;
            accepted_.fetch_add(accepted, std::memory_order_relaxed);
            dropped_.fetch_add(frames - accepted, std::memory_order_relaxed);
            nextPosition_ = position + static_cast<std::int64_t>(frames);
        }
    }
    gate_.fetch_and(~2u, std::memory_order_release);
}
void Recorder::drain(std::stop_token token) {
    std::array<float, 16384> block;
    bool diskFailed = false;
    auto lastPatch = std::chrono::steady_clock::now();
    std::int64_t patchedFrames = 0;
    for (;;) {
        const auto count = ring_->tryRead(block.data(), block.size());
        if (count) {
            if (!diskFailed && !writer_.write(block.data(), count / channels())) {
                diskFailed = true;
                failed_.store(true, std::memory_order_relaxed);
                log::error("record", "{}", writer_.error());
            }
        } else if (token.stop_requested()) {
            break;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto now = std::chrono::steady_clock::now();
        if (!diskFailed && now - lastPatch >= std::chrono::seconds(1) &&
            writer_.frames() != patchedFrames) {
            if (!writer_.patch()) {
                diskFailed = true;
                failed_.store(true, std::memory_order_relaxed);
                log::error("record", "{}", writer_.error());
            }
            patchedFrames = writer_.frames();
            lastPatch = now;
        }
    }
    if (!writer_.close()) {
        failed_.store(true, std::memory_order_relaxed);
        log::error("record", "{}", writer_.error());
    }
}
RecordedTake Recorder::stop() {
    gate_.fetch_and(~1u, std::memory_order_acq_rel);
    while (gate_.load(std::memory_order_acquire) & 2)
        std::this_thread::yield();
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
        error_ = writer_.error();
        dropped_.fetch_add(accepted_.load(std::memory_order_relaxed) -
                               static_cast<std::uint64_t>(writer_.frames()),
                           std::memory_order_relaxed);
        if (droppedFrames())
            log::warn("record", "Recording stopped at a discontinuity; {} frames dropped",
                      droppedFrames());
    }
    const auto start = start_.load(std::memory_order_relaxed);
    return {path_,
            start < 0 ? 0
                      : std::max<std::int64_t>(
                            0, start - static_cast<std::int64_t>(std::llround(latency_))),
            writer_.frames(),
            channels(),
            rate_,
            droppedFrames()};
}
} // namespace wavy::record
