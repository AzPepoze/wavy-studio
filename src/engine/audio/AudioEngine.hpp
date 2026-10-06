#pragma once
#include "audio/Mixer.hpp"
#include "record/Recorder.hpp"
#include <memory>
#include <string>
#include <vector>

namespace wavy {
struct InputDeviceInfo {
    unsigned id;
    std::string name;
    unsigned inputChannels;
    std::vector<unsigned> sampleRates;
    bool isDefault;
};
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
    std::string outputApiName() const;
    std::vector<InputDeviceInfo> inputDevices() const;
    bool setInputDevice(unsigned id, unsigned firstChannel, unsigned channelCount);
    bool inputAvailable() const;
    // RtAudio supplies a duplex round-trip total, charged to input alignment; output is
    // zero in duplex mode. Output-only streams report their actual output latency.
    double inputLatencyFrames() const;
    double outputLatencyFrames() const;
    void setMonitorGain(float gain) noexcept;
    void setMonitorEnabled(bool enabled) noexcept;
    record::Recorder& recorder();
    Mixer& mixer();
    // Requires no active device callback; Mixer has a single render consumer.
    void renderOffline(float* out, std::size_t frames);
    // Requires no active device callback. Output is stereo; null output discards monitoring.
    void feedInputOffline(const float* input, std::size_t frames, float* output = nullptr,
                          unsigned inputChannels = 2);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace wavy
