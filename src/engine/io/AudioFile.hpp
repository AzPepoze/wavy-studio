#pragma once
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string_view>
#include <vector>

namespace wavy {
struct AudioBuffer {
    unsigned sampleRate;
    unsigned channels;
    std::vector<float> samples; // Interleaved.
    std::int64_t frames() const;
};
enum class LoadError { FileNotFound, UnsupportedFormat, DecodeFailed };
std::string_view toString(LoadError error);
std::expected<AudioBuffer, LoadError> loadAudioFile(const std::filesystem::path& path,
                                                    unsigned targetSampleRate);
} // namespace wavy
