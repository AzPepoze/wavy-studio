#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace wavy::record {
class WavWriter {
  public:
    WavWriter() = default;
    ~WavWriter();
    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;
    bool open(const std::filesystem::path&, unsigned channels, unsigned sampleRate);
    bool write(const float* samples, std::size_t frames);
    bool patch();
    bool close(bool finalize = true);
    std::int64_t frames() const noexcept {
        return static_cast<std::int64_t>(bytes_ / (channels_ * 4));
    }
    const std::string& error() const noexcept { return error_; }

  private:
    bool fail(const char* message);
    std::FILE* file_ = nullptr;
    std::array<char, 65536> buffer_{};
    unsigned channels_ = 1, rate_ = 48000;
    std::uint64_t bytes_ = 0, patched_ = 0;
    std::string error_;
};
} // namespace wavy::record
