#include "audio/Mixer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace wavy {
Mixer::~Mixer() {
    delete current_;
    delete fading_;
    delete pending_.load();
    collectRetired();
}
void Mixer::collectRetired() { delete retired_.exchange(nullptr, std::memory_order_acquire); }
void Mixer::publish(std::unique_ptr<Snapshot> snapshot) {
    if (snapshot) {
        snapshot->scratch.resize(snapshot->maxBlockFrames * 2);
        for (auto& track : snapshot->tracks)
            track.scratch.resize(snapshot->maxBlockFrames * 2);
    }
    collectRetired();
    delete pending_.exchange(snapshot.release(), std::memory_order_acq_rel);
}
namespace {
bool sameChain(const std::vector<PreparedEffect>& a,
               const std::vector<PreparedEffect>& b) noexcept {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](const auto& x, const auto& y) {
               return x.effect.get() == y.effect.get();
           });
}
void process(const std::vector<PreparedEffect>& chain, float* out, std::size_t frames) noexcept {
    for (const auto& slot : chain)
        if (!slot.bypassed)
            slot.effect->process(out, frames);
}
void dryMix(Snapshot::Track& track, bool anySolo, unsigned rate, float* destination,
            std::size_t frames, std::int64_t position) noexcept {
    if (track.muted || (anySolo && !track.solo))
        return;
    const auto end = position + static_cast<std::int64_t>(frames);
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
        const auto minimum = std::max<std::int64_t>(1, rate / 1000);
        const auto in = std::max(clip.fadeIn, minimum);
        const auto fadeLength = std::min(clip.length, playable);
        const auto fade = std::max(clip.fadeOut, minimum);
        const auto edgeCount = std::min(minimum + 1, playable);
        const bool cachedEdges =
            clip.edges.source == clip.source.get() && clip.edges.offset == clip.sourceOffset &&
            clip.edges.length == playable && clip.fadeIn <= minimum && clip.fadeOut <= minimum;
        const auto fadeInEnd = clip.start + std::clamp(in, begin - clip.start, finish - clip.start);
        const auto fadeOutBegin =
            clip.start + std::clamp(fadeLength - 1 - fade, begin - clip.start, finish - clip.start);
        std::array boundaries{begin, std::min(fadeInEnd, fadeOutBegin),
                              std::max(fadeInEnd, fadeOutBegin), finish};
        for (std::size_t region = 0; region < 3; ++region) {
            const auto first = boundaries[region], last = boundaries[region + 1];
            if (first == last)
                continue;
            const auto local = first - clip.start;
            const bool fadeIn = local < in;
            const bool fadeOut = local >= fadeLength - 1 - fade;
            const auto* source =
                clip.source->samples.data() + (clip.sourceOffset + local) * clip.source->channels;
            auto* dest = destination + (first - position) * 2;
            const auto count = last - first;
            if (!fadeIn && !fadeOut) {
                const float steady = static_cast<float>(gain);
                if (clip.source->channels == 1) {
                    for (std::int64_t i = 0; i < count; ++i) {
                        dest[i * 2] += source[i] * steady;
                        dest[i * 2 + 1] += source[i] * steady;
                    }
                } else if (clip.source->channels == 2) {
                    for (std::int64_t i = 0; i < count * 2; ++i)
                        dest[i] += source[i] * steady;
                } else {
                    for (std::int64_t i = 0; i < count; ++i) {
                        dest[i * 2] += source[i * clip.source->channels] * steady;
                        dest[i * 2 + 1] += source[i * clip.source->channels + 1] * steady;
                    }
                }
            } else if (cachedEdges &&
                       (local + count <= edgeCount || local >= playable - edgeCount)) {
                const auto offset =
                    local < edgeCount ? local : edgeCount + local - (playable - edgeCount);
                const auto* edge = clip.edges.samples.data() + offset * 2;
                const float steady = static_cast<float>(gain);
                for (std::int64_t i = 0; i < count * 2; ++i)
                    dest[i] += edge[i] * steady;
            } else {
                const double inScale = 1. / in, outScale = 1. / fade;
                for (std::int64_t i = 0; i < count; ++i) {
                    double ramp = gain;
                    if (fadeIn)
                        ramp *= static_cast<double>(local + i) * inScale;
                    if (fadeOut)
                        ramp *= static_cast<double>(fadeLength - 1 - local - i) * outScale;
                    const auto index = i * clip.source->channels;
                    dest[i * 2] = static_cast<float>(dest[i * 2] + ramp * source[index]);
                    dest[i * 2 + 1] = static_cast<float>(
                        dest[i * 2 + 1] +
                        ramp * source[index + (clip.source->channels == 1 ? 0 : 1)]);
                }
            }
        }
    }
}
} // namespace
void Mixer::mix(float* out, std::size_t frames, std::int64_t position) noexcept {
    if (!current_)
        return;
    if (!fading_) {
        for (auto& track : current_->tracks) {
            if (track.muted || (current_->anySolo && !track.solo))
                continue;
            const bool active = std::any_of(track.effects.begin(), track.effects.end(),
                                            [](const auto& slot) { return !slot.bypassed; });
            float* destination = active ? track.scratch.data() : out;
            if (active)
                std::fill_n(destination, frames * 2, 0.f);
            dryMix(track, current_->anySolo, current_->sampleRate, destination, frames, position);
            if (active) {
                process(track.effects, destination, frames);
                for (std::size_t i = 0; i < frames * 2; ++i)
                    out[i] += destination[i];
            }
        }
        process(current_->masterEffects, out, frames);
    } else {
        auto* oldOut = fading_->scratch.data();
        std::fill_n(oldOut, frames * 2, 0.f);
        auto weight = [&](std::size_t i) { return float(swapFrame_ + i) / float(swapFrames_); };
        for (auto& track : current_->tracks) {
            std::fill_n(track.scratch.data(), frames * 2, 0.f);
            dryMix(track, current_->anySolo, current_->sampleRate, track.scratch.data(), frames,
                   position);
            auto previous = std::find_if(fading_->tracks.begin(), fading_->tracks.end(),
                                         [&](const auto& t) { return t.id == track.id; });
            if (previous != fading_->tracks.end() && sameChain(previous->effects, track.effects)) {
                std::fill_n(previous->scratch.data(), frames * 2, 0.f);
                dryMix(*previous, fading_->anySolo, fading_->sampleRate, previous->scratch.data(),
                       frames, position);
                for (std::size_t i = 0; i < frames * 2; ++i)
                    track.scratch[i] = previous->scratch[i] +
                                       weight(i / 2) * (track.scratch[i] - previous->scratch[i]);
                // Shared state must advance once per sample, even when a slot changes bypass state.
                for (std::size_t i = 0; i < track.effects.size(); ++i) {
                    const auto& next = track.effects[i];
                    const auto& old = previous->effects[i];
                    if (next.bypassed && old.bypassed)
                        continue;
                    std::copy_n(track.scratch.data(), frames * 2, previous->scratch.data());
                    next.effect->process(track.scratch.data(), frames);
                    if (next.bypassed != old.bypassed)
                        for (std::size_t j = 0; j < frames * 2; ++j) {
                            const float wet = next.bypassed ? 1 - weight(j / 2) : weight(j / 2);
                            track.scratch[j] = previous->scratch[j] +
                                               wet * (track.scratch[j] - previous->scratch[j]);
                        }
                }
                const bool nextAudible = !track.muted && (!current_->anySolo || track.solo);
                const bool oldAudible = !previous->muted && (!fading_->anySolo || previous->solo);
                for (std::size_t i = 0; i < frames * 2; ++i) {
                    float sample = track.scratch[i];
                    if (!nextAudible || !oldAudible)
                        sample *= (oldAudible ? 1 - weight(i / 2) : 0) +
                                  (nextAudible ? weight(i / 2) : 0);
                    out[i] += sample;
                    oldOut[i] += sample;
                }
            } else if (!track.muted && (!current_->anySolo || track.solo)) {
                process(track.effects, track.scratch.data(), frames);
                for (std::size_t i = 0; i < frames * 2; ++i)
                    out[i] += track.scratch[i];
            }
        }
        for (auto& track : fading_->tracks) {
            if (track.muted || (fading_->anySolo && !track.solo))
                continue;
            auto next = std::find_if(current_->tracks.begin(), current_->tracks.end(),
                                     [&](const auto& t) { return t.id == track.id; });
            if (next != current_->tracks.end() && sameChain(track.effects, next->effects))
                continue;
            std::fill_n(track.scratch.data(), frames * 2, 0.f);
            dryMix(track, fading_->anySolo, fading_->sampleRate, track.scratch.data(), frames,
                   position);
            process(track.effects, track.scratch.data(), frames);
            for (std::size_t i = 0; i < frames * 2; ++i)
                oldOut[i] += track.scratch[i];
        }
        if (sameChain(current_->masterEffects, fading_->masterEffects)) {
            for (std::size_t i = 0; i < frames * 2; ++i)
                out[i] = oldOut[i] + weight(i / 2) * (out[i] - oldOut[i]);
            for (std::size_t i = 0; i < current_->masterEffects.size(); ++i) {
                const auto& next = current_->masterEffects[i];
                const auto& old = fading_->masterEffects[i];
                if (next.bypassed && old.bypassed)
                    continue;
                std::copy_n(out, frames * 2, oldOut);
                next.effect->process(out, frames);
                if (next.bypassed != old.bypassed)
                    for (std::size_t j = 0; j < frames * 2; ++j) {
                        const float wet = next.bypassed ? 1 - weight(j / 2) : weight(j / 2);
                        out[j] = oldOut[j] + wet * (out[j] - oldOut[j]);
                    }
            }
        } else {
            process(current_->masterEffects, out, frames);
            process(fading_->masterEffects, oldOut, frames);
            for (std::size_t i = 0; i < frames * 2; ++i)
                out[i] = oldOut[i] + weight(i / 2) * (out[i] - oldOut[i]);
        }
    }
    for (std::size_t i = 0; i < frames * 2; ++i)
        if (!std::isfinite(out[i]))
            out[i] = 0;
}
void Mixer::render(float* out, std::size_t frames) noexcept {
    if (!frames)
        return;
    if (!fading_ && !retired_.load(std::memory_order_acquire)) {
        if (auto next = pending_.exchange(nullptr, std::memory_order_acquire)) {
            auto old = current_;
            current_ = next;
            if (!old)
                gainFrame_ = 0;
            if (old) {
                if (gainFrame_) {
                    fading_ = old;
                    swapFrame_ = 0;
                    swapFrames_ = std::max<std::size_t>(1, current_->sampleRate / 250);
                } else
                    retired_.store(old, std::memory_order_release);
            }
        }
    }
    for (auto request = transport_.requestedSeek_.load(); request >= 0;) {
        transport_.pendingSeek_.store(request);
        if (transport_.requestedSeek_.compare_exchange_weak(request, -1)) {
            seek_ = request;
            break;
        }
    }
    const bool playing = transport_.playing_.load();
    const auto rampFrames =
        std::max<std::size_t>(1, (current_ ? current_->sampleRate : 48000) * 3 / 1000);
    gainFrame_ = std::min(gainFrame_, rampFrames);
    const auto version = transport_.loopVersion_.load();
    const auto begin = transport_.loopBegin_.load();
    const auto end = transport_.loopEnd_.load();
    // Ignore concurrent loop edits rather than spinning on the control thread.
    const bool loop = transport_.loopEnabled_.load() && !(version & 1) &&
                      version == transport_.loopVersion_.load() && begin >= 0 && end > begin;
    std::fill_n(out, frames * 2, 0.f);
    std::size_t rendered = 0;
    while (rendered < frames) {
        if (!gainFrame_ && seek_ >= 0) {
            position_ = seek_;
            seek_ = -1;
        }
        if (!playing && !gainFrame_)
            break;
        if (loop && position_ >= end)
            position_ = begin + (position_ - begin) % (end - begin);
        const bool down = !playing || seek_ >= 0;
        const auto available = (loop ? end : std::numeric_limits<std::int64_t>::max()) - position_;
        auto count = std::min({frames - rendered, static_cast<std::size_t>(available),
                               current_ ? current_->maxBlockFrames : frames});
        if (fading_)
            count = std::min({count, fading_->maxBlockFrames, swapFrames_ - swapFrame_});
        if (down)
            count = std::min(count, gainFrame_);
        else if (gainFrame_ < rampFrames)
            count = std::min(count, rampFrames - gainFrame_);
        if (!count)
            break;
        auto* destination = out + rendered * 2;
        mix(destination, count, position_);
        if (down || gainFrame_ < rampFrames) {
            for (std::size_t i = 0; i < count; ++i) {
                const float gain = float(gainFrame_) / float(rampFrames);
                destination[i * 2] *= gain;
                destination[i * 2 + 1] *= gain;
                if (down)
                    --gainFrame_;
                else
                    ++gainFrame_;
            }
        }
        position_ += static_cast<std::int64_t>(count);
        rendered += count;
        if (fading_) {
            swapFrame_ += count;
            if (swapFrame_ == swapFrames_) {
                retired_.store(fading_, std::memory_order_release);
                fading_ = nullptr;
            }
        }
    }
    if (!gainFrame_ && seek_ >= 0) {
        position_ = seek_;
        seek_ = -1;
    }
    if (!gainFrame_ && fading_) {
        retired_.store(fading_, std::memory_order_release);
        fading_ = nullptr;
    }
    if (loop && position_ == end)
        position_ = begin;
    transport_.position_.store(position_);
    if (seek_ < 0)
        transport_.pendingSeek_.store(-1);
    transport_.audible_.store(gainFrame_ != 0);
}
} // namespace wavy
