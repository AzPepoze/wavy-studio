#pragma once
#include <atomic>
#include <cstdint>

namespace wavy {
class Transport {
  public:
    void play() noexcept { playing_.store(true); }
    void pause() noexcept { playing_.store(false); }
    void stop() noexcept {
        pause();
        seek(0);
    }
    void seek(std::int64_t frame) noexcept { requestedSeek_.store(frame < 0 ? 0 : frame); }
    // The logical position: a requested seek is reported at once, also while the audio thread is
    // still ramping the old audio down before it jumps, so callers never see a stale position.
    std::int64_t positionFrames() const noexcept {
        if (const auto seek = requestedSeek_.load(); seek >= 0)
            return seek;
        if (const auto pending = pendingSeek_.load(); pending >= 0)
            return pending;
        return position_.load();
    }
    // The timeline position of the next frame the audio thread renders. A seek on a silent
    // transport is applied by that very render, while an audible one is still ramping down at the
    // old position. The recorder aligns its input with this, not with the logical position.
    std::int64_t nextRenderedFrame() const noexcept {
        const auto seek = requestedSeek_.load();
        return !audible_.load() && seek >= 0 ? seek : position_.load();
    }
    bool isPlaying() const noexcept { return playing_.load(); }
    void setLoop(std::int64_t begin, std::int64_t end, bool enabled = true) noexcept;

  private:
    friend class Mixer;
    std::atomic<std::int64_t> position_{0}, loopBegin_{0}, loopEnd_{0};
    // requestedSeek_ is written by the control thread; the audio thread publishes the target in
    // pendingSeek_ before taking it, and clears it only after position_ holds the new position.
    std::atomic<std::int64_t> requestedSeek_{-1}, pendingSeek_{-1};
    std::atomic<bool> playing_{false}, audible_{false}, loopEnabled_{false};
    std::atomic<unsigned> loopVersion_{0};
};
static_assert(std::atomic<std::int64_t>::is_always_lock_free);
static_assert(std::atomic<unsigned>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
} // namespace wavy
