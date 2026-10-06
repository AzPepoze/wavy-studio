#pragma once
#include "audio/Mixer.hpp"
#include <memory>
#include <string>
#include <vector>

namespace wavy {
// Control methods must be called on one thread; audio callbacks never access the UI.
class AudioEngine {
  public:
    enum class DeviceMode { Default, NoDevice };
    explicit AudioEngine(DeviceMode mode = DeviceMode::Default);
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    bool start();
    void stop();
    unsigned int sampleRate() const;
    bool isRunning() const;
    static std::vector<std::string> availableApis();
    Mixer& mixer();
    // Requires no active device callback; Mixer has a single render consumer.
    void renderOffline(float* out, std::size_t frames);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace wavy
