#pragma once
#include "audio/Snapshot.hpp"
#include "audio/Transport.hpp"
#include <atomic>
#include <cstddef>
#include <memory>

namespace wavy {
class Mixer {
  public:
    Mixer() = default;
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;
    void publish(std::unique_ptr<Snapshot>);
    void collectRetired();
    void render(float* interleavedStereoOut, std::size_t frames) noexcept;
    Transport& transport() noexcept { return transport_; }

  private:
    void mix(float* out, std::size_t frames, std::int64_t position) noexcept;
    // One control producer and one render consumer. A full retire slot defers adoption;
    // only the control thread deletes snapshots. Destruction requires the consumer stopped.
    std::atomic<Snapshot*> pending_{nullptr}, retired_{nullptr};
    Snapshot* current_ = nullptr;
    Snapshot* fading_ = nullptr;
    std::size_t swapFrame_ = 0, swapFrames_ = 1, gainFrame_ = 0;
    std::int64_t position_ = 0, seek_ = -1;
    Transport transport_;
};
static_assert(std::atomic<Snapshot*>::is_always_lock_free);
} // namespace wavy
