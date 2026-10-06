#include "audio/Mixer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace wavy {
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
    for (auto& track : current_->tracks) {
        if (track.muted || (current_->anySolo && !track.solo))
            continue;
        const bool active = std::any_of(track.effects.begin(), track.effects.end(),
                                        [](const auto& slot) { return !slot.bypassed; });
        float* destination = active ? track.scratch.data() : out;
        if (active)
            std::fill_n(destination, frames * 2, 0.f);
        for (const auto& clip : track.clips) {
            if (clip.start >= end)
                break;
            const auto begin = std::max(position, clip.start);
            if (clip.sourceOffset >= clip.source->frames())
                continue;
            const auto playable = std::min(clip.length, clip.source->frames() - clip.sourceOffset);
            const auto finish = std::min(end, clip.start + playable);
            if (begin >= finish)
                continue;
            const double gain = static_cast<double>(track.gain) * clip.gain;
            const auto fadeInEnd =
                clip.start + std::clamp(clip.fadeIn, begin - clip.start, finish - clip.start);
            const auto fadeOutBegin =
                clip.start +
                std::clamp(clip.length - 1 - clip.fadeOut, begin - clip.start, finish - clip.start);
            std::array boundaries{begin, std::min(fadeInEnd, fadeOutBegin),
                                  std::max(fadeInEnd, fadeOutBegin), finish};
            for (std::size_t region = 0; region < 3; ++region) {
                const auto first = boundaries[region], last = boundaries[region + 1];
                const auto local = first - clip.start;
                const bool fadeIn = clip.fadeIn && local < clip.fadeIn;
                const bool fadeOut = clip.fadeOut && local >= clip.length - 1 - clip.fadeOut;
                const auto* source = clip.source->samples.data() +
                                     (clip.sourceOffset + local) * clip.source->channels;
                auto* dest = destination + (first - position) * 2;
                const auto count = last - first;
                if (!fadeIn && !fadeOut) {
                    const float steady = static_cast<float>(gain);
                    if (clip.source->channels == 1) {
                        for (std::int64_t i = 0; i < count; ++i) {
                            dest[i * 2] += source[i] * steady;
                            dest[i * 2 + 1] += source[i] * steady;
                        }
                    } else {
                        for (std::int64_t i = 0; i < count * 2; ++i)
                            dest[i] += source[i] * steady;
                    }
                } else {
                    for (std::int64_t i = 0; i < count; ++i) {
                        double ramp = gain;
                        if (fadeIn)
                            ramp *= static_cast<double>(local + i) / clip.fadeIn;
                        if (fadeOut)
                            ramp *= static_cast<double>(clip.length - 1 - local - i) / clip.fadeOut;
                        const auto index = i * clip.source->channels;
                        dest[i * 2] = static_cast<float>(dest[i * 2] + ramp * source[index]);
                        dest[i * 2 + 1] = static_cast<float>(
                            dest[i * 2 + 1] +
                            ramp * source[index + (clip.source->channels == 1 ? 0 : 1)]);
                    }
                }
            }
        }
        if (active) {
            for (auto& slot : track.effects)
                if (!slot.bypassed)
                    slot.effect->process(destination, frames);
            for (std::size_t i = 0; i < frames * 2; ++i)
                out[i] += destination[i];
        }
    }
    for (auto& slot : current_->masterEffects)
        if (!slot.bypassed)
            slot.effect->process(out, frames);
    for (std::size_t i = 0; i < frames * 2; ++i)
        if (!std::isfinite(out[i]))
            out[i] = 0;
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
        const auto capacity = current_ ? current_->maxBlockFrames : frames;
        const auto count =
            std::min({frames - rendered, static_cast<std::size_t>(available), capacity});
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
