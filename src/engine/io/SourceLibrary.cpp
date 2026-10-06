#include "io/SourceLibrary.hpp"
#include "core/Log.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <numbers>
#include <random>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace wavy {
namespace {
AudioBuffer generate(std::string_view spec, unsigned rate, std::stop_token stop) {
    spec.remove_prefix(10);
    const auto colon = spec.find(':');
    const auto kind = spec.substr(0, colon);
    if (kind != "sine" && kind != "saw" && kind != "square" && kind != "noise")
        throw std::invalid_argument("unknown generated waveform");
    double values[] = {440, 0.2, 10};
    if (colon == spec.npos && kind != "noise")
        throw std::invalid_argument("generated waveform requires a frequency");
    if (colon != spec.npos) {
        spec.remove_prefix(colon + 1);
        for (unsigned i = 0;; ++i) {
            if (i == 3)
                throw std::invalid_argument("too many generated parameters");
            const auto end = spec.find(':');
            const auto field = spec.substr(0, end);
            const auto parsed =
                std::from_chars(field.data(), field.data() + field.size(), values[i]);
            if (parsed.ec != std::errc{} || parsed.ptr != field.data() + field.size() ||
                !std::isfinite(values[i]))
                throw std::invalid_argument("invalid generated number");
            if (end == spec.npos)
                break;
            spec.remove_prefix(end + 1);
        }
    }
    const auto [hz, amplitude, seconds] = values;
    if (hz <= 0 || hz >= rate / 2.0 || amplitude < 0 || amplitude > 1 || seconds <= 0)
        throw std::invalid_argument("generated frequency, amplitude or duration out of range");
    const double frames = std::floor(seconds * rate);
    AudioBuffer audio{rate, 2, {}};
    if (!std::isfinite(frames) || frames < 1 ||
        frames >= static_cast<double>(audio.samples.max_size() / 2) ||
        frames >= static_cast<double>(std::numeric_limits<std::int64_t>::max()))
        throw std::invalid_argument("generated duration out of range");
    audio.samples.resize(static_cast<std::size_t>(frames) * 2);
    std::mt19937 random(0);
    for (std::size_t i = 0; i < audio.samples.size() / 2; ++i) {
        if (i % 4096 == 0 && stop.stop_requested())
            return {};
        const double phase = std::fmod(i * hz / rate, 1.0);
        const double wave = kind == "sine"     ? std::sin(2 * std::numbers::pi * phase)
                            : kind == "saw"    ? 2 * phase - 1
                            : kind == "square" ? (phase < 0.5 ? 1 : -1)
                                               : 2.0 * random() / std::mt19937::max() - 1;
        audio.samples[2 * i] = audio.samples[2 * i + 1] = static_cast<float>(amplitude * wave);
    }
    return audio;
}
std::size_t bytes(const Source& source) {
    std::size_t result = source.audio->samples.capacity() * sizeof(float);
    // PeakPyramid groups 64 frames, then four buckets per level. Allow vector growth overhead.
    for (auto buckets = (source.audio->frames() + 63) / 64; buckets; buckets = (buckets + 3) / 4) {
        result += static_cast<std::size_t>(buckets) * sizeof(PeakPair) +
                  2 * sizeof(std::vector<PeakPair>);
        if (buckets == 1)
            break;
    }
    return result;
}
} // namespace
struct SourceLibrary::Impl {
    struct Entry {
        State state = State::Loading;
        LoadError error = LoadError::DecodeFailed;
        std::shared_ptr<const Source> source;
        std::size_t bytes = 0;
        std::size_t used = 0;
    };
    unsigned rate;
    std::size_t budget, memory = 0, clock = 0, active = 0;
    mutable std::mutex mutex;
    std::condition_variable_any work;
    std::condition_variable idle;
    std::unordered_map<std::string, Entry> entries;
    std::deque<std::string> pending;
    std::shared_ptr<const std::function<void(const std::string&, bool)>> callback;
    bool stopping = false;
    std::vector<std::jthread> workers;

    Impl(unsigned rate, std::size_t count, std::size_t budget) : rate(rate), budget(budget) {
        if (!rate)
            throw std::invalid_argument("source library sample rate must be positive");
        if (!count)
            count = std::min(4u, std::max(1u, std::thread::hardware_concurrency()));
        workers.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
            workers.emplace_back([this](std::stop_token stop) { run(stop); });
    }
    ~Impl() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
            pending.clear();
        }
        for (auto& worker : workers)
            worker.request_stop();
        work.notify_all();
        // Join before destroying any state accessed by workers.
        workers.clear();
    }
    void evict() {
        while (memory > budget) {
            auto oldest = entries.end();
            for (auto it = entries.begin(); it != entries.end(); ++it) {
                const auto& source = it->second.source;
                if (source && source.use_count() == 1 && source->audio.use_count() == 1 &&
                    source->peaks.use_count() == 1 &&
                    (oldest == entries.end() || it->second.used < oldest->second.used))
                    oldest = it;
            }
            if (oldest == entries.end())
                break;
            memory -= oldest->second.bytes;
            entries.erase(oldest);
        }
    }
    void run(std::stop_token stop) {
        for (;;) {
            std::string path;
            {
                std::unique_lock lock(mutex);
                if (!work.wait(lock, stop, [this] { return stopping || !pending.empty(); }) ||
                    stopping)
                    return;
                path = std::move(pending.front());
                pending.pop_front();
                ++active;
            }
            std::shared_ptr<const Source> source;
            LoadError error = LoadError::DecodeFailed;
            try {
                auto audio = path.starts_with("generated:")
                                 ? std::expected<AudioBuffer, LoadError>(generate(path, rate, stop))
                                 : loadAudioFile(path, rate);
                if (audio && !stop.stop_requested()) {
                    auto buffer = std::make_shared<const AudioBuffer>(std::move(*audio));
                    auto peaks = std::make_shared<const PeakPyramid>(*buffer);
                    source = std::make_shared<const Source>(
                        Source{path, std::move(buffer), std::move(peaks)});
                    log::info("source-library", "loaded {}", path);
                } else if (!audio) {
                    error = audio.error();
                    log::warn("source-library", "{}: {}", path, toString(error));
                }
            } catch (const std::exception& exception) {
                log::warn("source-library", "{}: {}", path, exception.what());
            } catch (...) {
                log::warn("source-library", "{}: unknown load exception", path);
            }
            const bool ok = bool(source);
            std::shared_ptr<const std::function<void(const std::string&, bool)>> notify;
            {
                std::lock_guard lock(mutex);
                if (!stopping) {
                    auto& entry = entries.at(path);
                    entry.state = ok ? State::Ready : State::Failed;
                    entry.error = error;
                    entry.bytes = ok ? bytes(*source) : 0;
                    entry.used = ++clock;
                    memory += entry.bytes;
                    entry.source = std::move(source);
                    evict();
                    notify = callback;
                }
            }
            if (notify) {
                try {
                    (*notify)(path, ok);
                } catch (...) {
                    log::warn("source-library", "{}: ready callback threw", path);
                }
            }
            {
                std::lock_guard lock(mutex);
                --active;
                if (!active && pending.empty())
                    idle.notify_all();
            }
        }
    }
};
SourceLibrary::SourceLibrary(unsigned rate, std::size_t workers, std::size_t budget)
    : impl_(std::make_unique<Impl>(rate, workers, budget)) {}
SourceLibrary::~SourceLibrary() = default;
void SourceLibrary::request(const std::string& path) {
    std::lock_guard lock(impl_->mutex);
    impl_->evict();
    auto [it, inserted] = impl_->entries.try_emplace(path);
    it->second.used = ++impl_->clock;
    if (!inserted)
        return;
    try {
        impl_->pending.push_back(path);
    } catch (...) {
        impl_->entries.erase(it);
        throw;
    }
    impl_->work.notify_one();
}
std::shared_ptr<const Source> SourceLibrary::get(const std::string& path) const {
    std::lock_guard lock(impl_->mutex);
    const auto it = impl_->entries.find(path);
    if (it == impl_->entries.end())
        return nullptr;
    it->second.used = ++impl_->clock;
    return it->second.source;
}
SourceLibrary::State SourceLibrary::state(const std::string& path) const {
    std::lock_guard lock(impl_->mutex);
    const auto it = impl_->entries.find(path);
    return it == impl_->entries.end() ? State::Unknown : it->second.state;
}
LoadError SourceLibrary::error(const std::string& path) const {
    std::lock_guard lock(impl_->mutex);
    const auto it = impl_->entries.find(path);
    return it == impl_->entries.end() ? LoadError::DecodeFailed : it->second.error;
}
void SourceLibrary::setOnReady(std::function<void(const std::string&, bool)> callback) {
    auto next =
        callback ? std::make_shared<const decltype(callback)>(std::move(callback)) : nullptr;
    std::lock_guard lock(impl_->mutex);
    impl_->callback = std::move(next);
}
std::size_t SourceLibrary::memoryBytes() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->memory;
}
void SourceLibrary::waitIdle() {
    std::unique_lock lock(impl_->mutex);
    impl_->idle.wait(lock, [this] { return impl_->pending.empty() && !impl_->active; });
}
} // namespace wavy
