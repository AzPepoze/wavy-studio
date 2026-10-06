#include "audio/AudioEngine.hpp"
#include "core/Log.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <limits>
#include <rtaudio/RtAudio.h>

namespace wavy {
struct AudioEngine::Impl {
    DeviceMode mode;
    Mixer mixer;
    record::Recorder recorder;
    unsigned inputId = 0, firstChannel = 0, inputChannels = 2;
    bool selectedInput = false, hasInput = false;
    double inputLatency = 0, outputLatency = 0;
    std::atomic<float> monitorGain{0};
    std::atomic<bool> monitorEnabled{false};
    std::atomic<bool> deviceFailed{false};
    std::unique_ptr<RtAudio> device;
    unsigned int rate = 48000;
    bool running = false;
    explicit Impl(DeviceMode value) : mode(value) {}
    void process(const float* input, float* output, std::size_t frames, unsigned channels,
                 bool inputLost = false) noexcept {
        if (!output) {
            std::array<float, 512> scratch;
            for (std::size_t offset = 0; offset < frames;) {
                const auto count = std::min<std::size_t>(256, frames - offset);
                process(input ? input + offset * channels : nullptr, scratch.data(), count,
                        channels, inputLost);
                offset += count;
            }
            return;
        }
        if ((input || inputLost) && recorder.isArmed() && mixer.transport().isPlaying()) {
            // Per-frame transport reads follow loops and seeks through the existing mixer path.
            for (std::size_t frame = 0; frame < frames; ++frame) {
                if (mixer.transport().isPlaying()) {
                    float sample[2]{};
                    if (input) {
                        sample[0] = input[frame * channels];
                        sample[1] = input[frame * channels + (channels == 1 ? 0 : 1)];
                    }
                    if (recorder.channels() == 1 && channels == 2)
                        sample[0] = sample[0] * 0.5f + sample[1] * 0.5f;
                    recorder.capture(inputLost ? nullptr : sample, 1,
                                     mixer.transport().nextRenderedFrame());
                }
                mixer.render(output + frame * 2, 1);
            }
        } else {
            mixer.render(output, frames);
        }
        const auto gain = monitorGain.load(std::memory_order_relaxed);
        if (input && monitorEnabled.load(std::memory_order_relaxed) && gain != 0) {
            for (std::size_t frame = 0; frame < frames; ++frame)
                for (unsigned channel = 0; channel < 2; ++channel) {
                    const double value =
                        output[frame * 2 + channel] +
                        double(gain) * input[frame * channels + (channels == 1 ? 0 : channel)];
                    if (std::isfinite(value))
                        output[frame * 2 + channel] = static_cast<float>(
                            std::clamp(value, -double(std::numeric_limits<float>::max()),
                                       double(std::numeric_limits<float>::max())));
                }
        }
    }
    static int render(void* output, void* input, unsigned int frames, double,
                      RtAudioStreamStatus status, void* state) noexcept {
        auto& engine = *static_cast<Impl*>(state);
        engine.process(static_cast<const float*>(input), static_cast<float*>(output), frames,
                       engine.inputChannels, (status & RTAUDIO_INPUT_OVERFLOW) != 0);
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
            RtAudio::StreamParameters input;
            input.deviceId = impl_->selectedInput ? impl_->inputId : device.getDefaultInputDevice();
            input.nChannels = impl_->inputChannels;
            input.firstChannel = impl_->firstChannel;
            bool wantsInput = input.deviceId != 0;
            if (wantsInput && !impl_->selectedInput)
                input.nChannels = impl_->inputChannels =
                    std::min(2u, device.getDeviceInfo(input.deviceId).inputChannels);
            if (wantsInput) {
                const auto info = device.getDeviceInfo(input.deviceId);
                wantsInput = input.firstChannel <= info.inputChannels &&
                             input.nChannels <= info.inputChannels - input.firstChannel;
            }
            auto open = [&](bool capture) {
                unsigned int frames = 256;
                impl_->deviceFailed.store(false, std::memory_order_relaxed);
                if (device.openStream(output.deviceId ? &output : nullptr,
                                      capture ? &input : nullptr, RTAUDIO_FLOAT32, 48000, &frames,
                                      Impl::render, impl_.get()) != RTAUDIO_NO_ERROR)
                    return false;
                impl_->rate = device.getStreamSampleRate();
                if (capture && !impl_->recorder.setSampleRate(impl_->rate))
                    return false;
                const auto latency = std::max(0L, device.getStreamLatency());
                impl_->inputLatency = capture ? latency : 0;
                impl_->outputLatency = capture ? 0 : latency;
                impl_->recorder.setInputLatencyFrames(impl_->inputLatency);
                return device.startStream() == RTAUDIO_NO_ERROR &&
                       !impl_->deviceFailed.load(std::memory_order_relaxed);
            };
            bool opened = wantsInput && open(true);
            impl_->hasInput = opened;
            if (!opened) {
                if (device.isStreamOpen())
                    device.closeStream();
                if (wantsInput)
                    log::warn("audio", "Input unavailable; retrying output only");
                opened = output.deviceId && open(false);
            }
            if (opened) {
                log::info(
                    "audio", "Device opened: {} at {} Hz",
                    device.getDeviceInfo(output.deviceId ? output.deviceId : input.deviceId).name,
                    impl_->rate);
            } else {
                impl_->device.reset();
                impl_->rate = 48000;
                impl_->hasInput = false;
                impl_->inputLatency = impl_->outputLatency = 0;
                impl_->recorder.setInputLatencyFrames(0);
            }
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
    impl_->recorder.stop();
    impl_->hasInput = false;
    impl_->inputLatency = impl_->outputLatency = 0;
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
    impl_->process(nullptr, out, frames, 2);
}
std::vector<InputDeviceInfo> AudioEngine::inputDevices() const {
    if (impl_->mode == DeviceMode::NoDevice)
        return {};
    RtAudio probe(RtAudio::UNSPECIFIED, [](RtAudioErrorType, const std::string&) {});
    auto& device = impl_->device ? *impl_->device : probe;
    std::vector<InputDeviceInfo> result;
    for (auto id : device.getDeviceIds()) {
        auto info = device.getDeviceInfo(id);
        if (info.inputChannels)
            result.push_back({id, std::move(info.name), info.inputChannels,
                              std::move(info.sampleRates), info.isDefaultInput});
    }
    return result;
}
bool AudioEngine::setInputDevice(unsigned id, unsigned firstChannel, unsigned channelCount) {
    if (impl_->running || (channelCount != 1 && channelCount != 2))
        return false;
    const auto devices = inputDevices();
    const auto found = std::find_if(devices.begin(), devices.end(),
                                    [id](const auto& info) { return info.id == id; });
    if (found == devices.end() || firstChannel > found->inputChannels ||
        channelCount > found->inputChannels - firstChannel)
        return false;
    impl_->inputId = id;
    impl_->firstChannel = firstChannel;
    impl_->inputChannels = channelCount;
    impl_->selectedInput = true;
    return true;
}
bool AudioEngine::inputAvailable() const {
    return impl_->hasInput && !impl_->deviceFailed.load(std::memory_order_relaxed);
}
double AudioEngine::inputLatencyFrames() const { return impl_->inputLatency; }
double AudioEngine::outputLatencyFrames() const { return impl_->outputLatency; }
void AudioEngine::setMonitorGain(float gain) noexcept {
    impl_->monitorGain.store(std::isfinite(gain) ? std::max(0.f, gain) : 0.f,
                             std::memory_order_relaxed);
}
void AudioEngine::setMonitorEnabled(bool enabled) noexcept {
    impl_->monitorEnabled.store(enabled, std::memory_order_relaxed);
}
record::Recorder& AudioEngine::recorder() { return impl_->recorder; }
void AudioEngine::feedInputOffline(const float* input, std::size_t frames, float* output,
                                   unsigned channels) {
    if (channels != 1 && channels != 2)
        return;
    impl_->process(input, output, frames, channels);
}
std::string AudioEngine::outputApiName() const {
    return impl_->device ? RtAudio::getApiDisplayName(impl_->device->getCurrentApi()) : "No device";
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
