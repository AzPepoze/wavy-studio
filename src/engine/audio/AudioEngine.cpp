#include "audio/AudioEngine.hpp"
#include "core/Log.hpp"
#include <atomic>
#include <cstddef>
#include <rtaudio/RtAudio.h>

namespace wavy {
struct AudioEngine::Impl {
    DeviceMode mode;
    Mixer mixer;
    std::atomic<bool> deviceFailed{false};
    std::unique_ptr<RtAudio> device;
    unsigned int rate = 48000;
    bool running = false;
    explicit Impl(DeviceMode value) : mode(value) {}
    static int render(void* output, void*, unsigned int frames, double, RtAudioStreamStatus,
                      void* state) noexcept {
        static_cast<Impl*>(state)->mixer.render(static_cast<float*>(output), frames);
        return 0;
    }
};
AudioEngine::AudioEngine(DeviceMode mode) : impl_(std::make_unique<Impl>(mode)) {}
AudioEngine::~AudioEngine() { stop(); }
bool AudioEngine::start() {
    if (impl_->running)
        return true;
    impl_->deviceFailed.store(false, std::memory_order_relaxed);
    if (impl_->mode == DeviceMode::Default) {
        impl_->device = std::make_unique<RtAudio>(
            RtAudio::UNSPECIFIED, [state = impl_.get()](RtAudioErrorType type, const std::string&) {
                if (type != RTAUDIO_WARNING && type != RTAUDIO_NO_ERROR)
                    state->deviceFailed.store(true, std::memory_order_relaxed);
            });
        auto& device = *impl_->device;
        log::debug("audio", "API: {}", RtAudio::getApiDisplayName(device.getCurrentApi()));
        if (device.getDeviceCount() > 0) {
            RtAudio::StreamParameters output;
            output.deviceId = device.getDefaultOutputDevice();
            output.nChannels = 2;
            output.firstChannel = 0;
            unsigned int frames = 256;
            if (device.openStream(&output, nullptr, RTAUDIO_FLOAT32, 48000, &frames, Impl::render,
                                  impl_.get()) == RTAUDIO_NO_ERROR &&
                device.startStream() == RTAUDIO_NO_ERROR &&
                !impl_->deviceFailed.load(std::memory_order_relaxed)) {
                impl_->rate = device.getStreamSampleRate();
                log::info("audio", "Device opened: {} at {} Hz",
                          device.getDeviceInfo(output.deviceId).name, impl_->rate);
            } else
                impl_->device.reset();
        } else {
            impl_->device.reset();
        }
    }
    if (impl_->mode == DeviceMode::Default && !impl_->device)
        log::warn("audio", "Falling back to no-device mode");
    log::debug("audio", "Engine started");
    // A failed device initialization/start is a valid silent, no-device session.
    impl_->running = true;
    return true;
}
void AudioEngine::stop() {
    if (impl_->running)
        log::debug("audio", "Engine stopped");
    impl_->device.reset();
    impl_->rate = 48000;
    impl_->running = false;
}
unsigned int AudioEngine::sampleRate() const {
    if (!impl_->running)
        return 0;
    return impl_->deviceFailed.load(std::memory_order_relaxed) ? 48000 : impl_->rate;
}
bool AudioEngine::isRunning() const { return impl_->running; }
Mixer& AudioEngine::mixer() { return impl_->mixer; }
void AudioEngine::renderOffline(float* out, std::size_t frames) {
    impl_->mixer.render(out, frames);
}
std::vector<std::string> AudioEngine::availableApis() {
    std::vector<RtAudio::Api> apis;
    RtAudio::getCompiledApi(apis);
    std::vector<std::string> names;
    for (auto api : apis)
        names.push_back(RtAudio::getApiDisplayName(api));
    return names;
}
} // namespace wavy
