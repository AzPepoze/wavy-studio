#pragma once
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

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace wavy
