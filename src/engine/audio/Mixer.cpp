#include "audio/Mixer.hpp"
#include "core/Log.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace wavy {
std::unique_ptr<Snapshot> buildSnapshot(const timeline::Timeline& timeline,
                                        const SourceCache& cache) {
    auto result = std::make_unique<Snapshot>();
    SourceCache loaded;
    for (const auto& track : timeline.tracks()) {
        Snapshot::Track copy{track.gain, track.muted, track.solo, {}};
        result->anySolo |= track.solo;
        for (const auto& clip : track.clips) {
            std::shared_ptr<const AudioBuffer> source;
            if (auto it = cache.find(clip.source); it != cache.end())
                source = it->second;
            else if (auto it = loaded.find(clip.source); it != loaded.end())
                source = it->second;
            else {
                auto audio = loadAudioFile(clip.source, timeline.sampleRate);
                if (audio)
                    source = std::make_shared<AudioBuffer>(std::move(*audio));
                else
                    log::error("mixer", "Cannot load {}: {}", clip.source, toString(audio.error()));
                loaded.emplace(clip.source, source);
            }
            if (!source || source->sampleRate != timeline.sampleRate || !source->channels) {
                log::error("mixer", "Missing or incompatible source: {}", clip.source);
                continue;
            }
            if (clip.start < 0 || clip.length <= 0 || clip.sourceOffset < 0 ||
                clip.start > std::numeric_limits<std::int64_t>::max() - clip.length)
                continue;
            copy.clips.push_back({clip.start, clip.length, clip.sourceOffset,
                                  std::max<std::int64_t>(0, clip.fadeIn),
                                  std::max<std::int64_t>(0, clip.fadeOut), clip.gain, source});
        }
        std::sort(copy.clips.begin(), copy.clips.end(),
                  [](const auto& a, const auto& b) { return a.start < b.start; });
        result->tracks.push_back(std::move(copy));
    }
    return result;
}
void Transport::setLoop(std::int64_t begin, std::int64_t end, bool enabled) noexcept {
    loopVersion_.fetch_add(1);
    loopBegin_.store(begin);
    loopEnd_.store(end);
    loopEnabled_.store(enabled && begin >= 0 && end > begin);
    loopVersion_.fetch_add(1);
}
Mixer::~Mixer() {
    delete current_;
    delete pending_.load();
    collectRetired();
}
void Mixer::collectRetired() { delete retired_.exchange(nullptr, std::memory_order_acquire); }
void Mixer::publish(std::unique_ptr<Snapshot> snapshot) {
    collectRetired();
    delete pending_.exchange(snapshot.release(), std::memory_order_acq_rel);
}
void Mixer::mix(float* out, std::size_t frames, std::int64_t position) noexcept {
    if (!current_)
        return;
    const auto end = position + static_cast<std::int64_t>(frames);
    for (const auto& track : current_->tracks) {
        if (track.muted || (current_->anySolo && !track.solo))
            continue;
        for (const auto& clip : track.clips) {
            if (clip.start >= end)
                break;
            const auto begin = std::max(position, clip.start);
            if (clip.sourceOffset >= clip.source->frames())
                continue;
            const auto playable = std::min(clip.length, clip.source->frames() - clip.sourceOffset);
            const auto finish = std::min(end, clip.start + playable);
            for (auto frame = begin; frame < finish; ++frame) {
                const auto local = frame - clip.start;
                double gain = static_cast<double>(track.gain) * clip.gain;
                if (clip.fadeIn)
                    gain *= std::min(1.0, static_cast<double>(local) / clip.fadeIn);
                if (clip.fadeOut)
                    gain *=
                        std::min(1.0, static_cast<double>(clip.length - 1 - local) / clip.fadeOut);
                const auto index =
                    static_cast<std::size_t>(clip.sourceOffset + local) * clip.source->channels;
                const auto dest = static_cast<std::size_t>(frame - position) * 2;
                for (unsigned channel = 0; channel < 2; ++channel) {
                    const double value =
                        out[dest + channel] +
                        gain * clip.source
                                   ->samples[index + (clip.source->channels == 1 ? 0 : channel)];
                    if (std::isfinite(value))
                        out[dest + channel] = static_cast<float>(std::clamp(
                            value, -static_cast<double>(std::numeric_limits<float>::max()),
                            static_cast<double>(std::numeric_limits<float>::max())));
                }
            }
        }
    }
}
void Mixer::render(float* out, std::size_t frames) noexcept {
    if (!retired_.load(std::memory_order_acquire)) {
        if (auto next = pending_.exchange(nullptr, std::memory_order_acquire)) {
            auto old = current_;
            current_ = next;
            if (old)
                retired_.store(old, std::memory_order_release);
        }
    }
    std::fill_n(out, frames * 2, 0.f);
    if (!transport_.playing_.load())
        return;
    auto original = transport_.position_.load();
    auto position = original;
    const auto version = transport_.loopVersion_.load();
    const auto begin = transport_.loopBegin_.load();
    const auto end = transport_.loopEnd_.load();
    // A concurrent loop edit is ignored for this block rather than spinning on the control thread.
    const bool loop = transport_.loopEnabled_.load() && !(version & 1) &&
                      version == transport_.loopVersion_.load() && begin >= 0 && end > begin;
    std::size_t rendered = 0;
    while (rendered < frames) {
        if (loop && position >= end)
            position = begin + (position - begin) % (end - begin);
        const auto available = (loop ? end : std::numeric_limits<std::int64_t>::max()) - position;
        const auto count = std::min(frames - rendered, static_cast<std::size_t>(available));
        if (!count)
            break;
        mix(out + rendered * 2, count, position);
        position += static_cast<std::int64_t>(count);
        rendered += count;
    }
    if (loop && position == end)
        position = begin;
    // A seek during rendering wins over the completed block's position update.
    transport_.position_.compare_exchange_strong(original, position);
}
} // namespace wavy
