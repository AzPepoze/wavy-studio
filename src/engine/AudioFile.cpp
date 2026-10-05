#include "AudioFile.hpp"
#include "Log.hpp"
#include <chrono>

namespace wavy {
std::expected<AudioBuffer, LoadError> decodeAudioFile(const std::filesystem::path&, unsigned);
std::int64_t AudioBuffer::frames() const {
    return channels ? static_cast<std::int64_t>(samples.size() / channels) : 0;
}
std::string_view toString(LoadError error) {
    switch (error) {
    case LoadError::FileNotFound:
        return "FileNotFound";
    case LoadError::UnsupportedFormat:
        return "UnsupportedFormat";
    case LoadError::DecodeFailed:
        return "DecodeFailed";
    }
    return "DecodeFailed";
}
std::expected<AudioBuffer, LoadError> loadAudioFile(const std::filesystem::path& path,
                                                    unsigned targetSampleRate) {
    const auto start = std::chrono::steady_clock::now();
    std::error_code error;
    if (!std::filesystem::exists(path, error) && !error) {
        log::warn("audio-file", "{}: {}", path.string(), toString(LoadError::FileNotFound));
        return std::unexpected(LoadError::FileNotFound);
    }
    auto result = decodeAudioFile(path, targetSampleRate);
    if (!result) {
        log::error("audio-file", "{}: {}", path.string(), toString(result.error()));
        return result;
    }
    const auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    log::info("audio-file", "{}: {} Hz, {} channels, {} frames, {:.2f} ms", path.string(),
              result->sampleRate, result->channels, result->frames(), elapsed);
    return result;
}
} // namespace wavy
