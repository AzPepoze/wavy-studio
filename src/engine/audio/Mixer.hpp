#pragma once
#include "io/AudioFile.hpp"
#include "timeline/Timeline.hpp"
#include <atomic>
#include <memory>
#include <unordered_map>

namespace wavy {
using SourceCache = std::unordered_map<std::string, std::shared_ptr<const AudioBuffer>>;
struct Snapshot {
    struct Clip {
        std::int64_t start, length, sourceOffset, fadeIn, fadeOut;
        float gain;
        std::shared_ptr<const AudioBuffer> source;
    };
    struct Track {
        float gain;
        bool muted, solo;
        std::vector<Clip> clips;
    };
    std::vector<Track> tracks;
    bool anySolo = false;
};
std::unique_ptr<Snapshot> buildSnapshot(const timeline::Timeline&, const SourceCache&);
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
    Transport transport_;
};
static_assert(std::atomic<Snapshot*>::is_always_lock_free);
static_assert(std::atomic<std::int64_t>::is_always_lock_free);
static_assert(std::atomic<unsigned>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
} // namespace wavy
