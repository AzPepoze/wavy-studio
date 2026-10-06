#pragma once
#include "record/RingBuffer.hpp"
#include "record/WavWriter.hpp"
#include <array>
#include <filesystem>
#include <thread>

namespace wavy::record {
struct RecordedTake {
    std::filesystem::path path;
    std::int64_t startFrame = 0;
    std::int64_t lengthFrames = 0;
    unsigned channels = 0;
    unsigned sampleRate = 0;
    std::uint64_t droppedFrames = 0;
};
// One control thread, one capture thread; stop waits only on the control thread.
class Recorder {
  public:
    explicit Recorder(unsigned sampleRate = 48000, std::size_t capacity = 0);
    ~Recorder();
    bool setSampleRate(unsigned sampleRate);
    bool arm(const std::filesystem::path&, unsigned channels);
    bool startAtFrame(std::int64_t timelineFrame);
    RecordedTake stop();
    void capture(const float* input, std::size_t frames, std::int64_t timelinePosition) noexcept;
    void setInputLatencyFrames(double frames);
    float peak(unsigned channel) const noexcept;
    unsigned channels() const noexcept { return channels_.load(std::memory_order_relaxed); }
    bool isArmed() const noexcept { return gate_.load(std::memory_order_acquire) & 1; }
    bool failed() const noexcept { return failed_.load(std::memory_order_relaxed); }
    std::uint64_t droppedFrames() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }
    const std::string& error() const noexcept { return error_; }

  private:
    void drain(std::stop_token);
    unsigned rate_;
    std::size_t capacity_;
    std::unique_ptr<RingBuffer> ring_;
    WavWriter writer_;
    std::jthread thread_;
    std::filesystem::path path_;
    std::string error_;
    double latency_ = 0;
    std::int64_t punch_ = 0, nextPosition_ = -1;
    bool overrun_ = false;
    std::atomic<unsigned> channels_{0};
    // Bit 0 admits capture, bit 1 marks its bounded critical section. No audio-thread waiting.
    std::atomic<unsigned> gate_{0};
    std::atomic<bool> failed_{false};
    std::atomic<std::int64_t> start_{-1};
    std::atomic<std::uint64_t> dropped_{0}, accepted_{0};
    std::array<std::atomic<float>, 2> peaks_{};
};
static_assert(std::atomic<float>::is_always_lock_free);
} // namespace wavy::record
