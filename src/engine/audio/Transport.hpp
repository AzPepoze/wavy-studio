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
    void seek(std::int64_t frame) noexcept { position_.store(frame < 0 ? 0 : frame); }
    std::int64_t positionFrames() const noexcept { return position_.load(); }
    bool isPlaying() const noexcept { return playing_.load(); }
    void setLoop(std::int64_t begin, std::int64_t end, bool enabled = true) noexcept;

  private:
    friend class Mixer;
    std::atomic<std::int64_t> position_{0}, loopBegin_{0}, loopEnd_{0};
    std::atomic<bool> playing_{false}, loopEnabled_{false};
    std::atomic<unsigned> loopVersion_{0};
};
static_assert(std::atomic<std::int64_t>::is_always_lock_free);
static_assert(std::atomic<unsigned>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
} // namespace wavy
