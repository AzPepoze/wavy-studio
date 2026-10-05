#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "AudioFile.hpp"
#include "Log.hpp"
#include "Peaks.hpp"
#include <bit>
#include <chrono>
#include <cmath>
#include <doctest/doctest.h>
#include <fstream>

namespace {
struct TempDirectory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("wavy-audio-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TempDirectory() { std::filesystem::create_directory(path); }
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
void integer(std::ostream& stream, std::uint32_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i)
        stream.put(static_cast<char>(value >> (8 * i)));
}
void wav(const std::filesystem::path& path, unsigned rate, unsigned channels,
         const std::vector<float>& samples, bool floating) {
    std::ofstream stream(path, std::ios::binary);
    const unsigned bytes = floating ? 4 : 2;
    const auto size = static_cast<std::uint32_t>(samples.size() * bytes);
    stream.write("RIFF", 4);
    integer(stream, 36 + size, 4);
    stream.write("WAVEfmt ", 8);
    integer(stream, 16, 4);
    integer(stream, floating ? 3 : 1, 2);
    integer(stream, channels, 2);
    integer(stream, rate, 4);
    integer(stream, rate * channels * bytes, 4);
    integer(stream, channels * bytes, 2);
    integer(stream, bytes * 8, 2);
    stream.write("data", 4);
    integer(stream, size, 4);
    for (float sample : samples)
        integer(stream,
                floating ? std::bit_cast<std::uint32_t>(sample)
                         : static_cast<std::uint16_t>(static_cast<std::int16_t>(sample * 32768)),
                bytes);
    REQUIRE(stream.good());
}
double milliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}
} // namespace
TEST_CASE("PCM and float WAV decode at native rates") {
    TempDirectory temp;
    for (unsigned rate : {44100u, 48000u})
        for (unsigned channels : {1u, 2u})
            for (bool floating : {false, true}) {
                std::vector<float> samples(257 * channels);
                for (std::size_t i = 0; i < samples.size(); ++i)
                    samples[i] = (static_cast<int>(i % 17) - 8) / 16.0f;
                const auto path = temp.path / "native.wav";
                wav(path, rate, channels, samples, floating);
                const auto loaded = wavy::loadAudioFile(path, 0);
                REQUIRE(loaded.has_value());
                CHECK(loaded->sampleRate == rate);
                CHECK(loaded->channels == channels);
                CHECK(loaded->frames() == 257);
                for (std::size_t i = 0; i < samples.size(); ++i)
                    CHECK(loaded->samples[i] == doctest::Approx(samples[i]).epsilon(0.0001));
            }
}
TEST_CASE("Resampling and load failures") {
    TempDirectory temp;
    wav(temp.path / "rate.wav", 44100, 1, std::vector<float>(44100, 0.25f), false);
    const auto loaded = wavy::loadAudioFile(temp.path / "rate.wav", 48000);
    REQUIRE(loaded.has_value());
    CHECK(loaded->sampleRate == 48000);
    CHECK(std::abs(loaded->frames() - 48000) <= 1);
    const auto missing = wavy::loadAudioFile(temp.path / "missing.wav", 0);
    REQUIRE_FALSE(missing.has_value());
    CHECK((missing.error() == wavy::LoadError::FileNotFound));
    {
        std::ofstream stream(temp.path / "garbage");
        stream << "garbage";
    }
    const auto garbage = wavy::loadAudioFile(temp.path / "garbage", 0);
    REQUIRE_FALSE(garbage.has_value());
    CHECK((garbage.error() == wavy::LoadError::UnsupportedFormat ||
           garbage.error() == wavy::LoadError::DecodeFailed));
}
TEST_CASE("Peak levels preserve ramp and square extrema and clamp ranges") {
    for (bool square : {false, true}) {
        wavy::AudioBuffer buffer{48000, 2, {}};
        for (int i = 0; i < 1024; ++i) {
            const float sample = square ? (i % 128 < 64 ? -1.0f : 1.0f) : i / 1024.0f;
            buffer.samples.insert(buffer.samples.end(), {sample, sample});
        }
        wavy::PeakPyramid peaks(buffer);
        std::vector<wavy::PeakPair> fine, coarse;
        peaks.query(0, 1024, 16, fine);
        peaks.query(0, 1024, 4, coarse);
        for (int i = 0; i < 16; ++i) {
            CHECK(fine[i].min == (square ? (i % 2 ? 1.0f : -1.0f) : i * 64 / 1024.0f));
            CHECK(fine[i].max == (square ? (i % 2 ? 1.0f : -1.0f) : (i * 64 + 63) / 1024.0f));
        }
        for (int i = 0; i < 4; ++i) {
            float min = fine[i * 4].min, max = fine[i * 4].max;
            for (int j = 1; j < 4; ++j) {
                min = std::min(min, fine[i * 4 + j].min);
                max = std::max(max, fine[i * 4 + j].max);
            }
            CHECK(coarse[i].min == min);
            CHECK(coarse[i].max == max);
        }
        peaks.query(-100, 2000, 1, coarse);
        CHECK(coarse[0].min == (square ? -1.0f : 0.0f));
        CHECK(coarse[0].max == (square ? 1.0f : 1023 / 1024.0f));
        peaks.query(2000, 3000, 3, coarse);
        REQUIRE(coarse.size() == 3);
        for (auto peak : coarse) {
            CHECK(peak.min == 0);
            CHECK(peak.max == 0);
        }
        peaks.query(0, 1024, 0, coarse);
        CHECK(coarse.empty());
    }
    wavy::PeakPyramid mixed(wavy::AudioBuffer{48000, 2, {-1, 1, -0.5f, 0.5f}});
    std::vector<wavy::PeakPair> out;
    mixed.query(0, 2, 1, out);
    // Opposite-polarity channels must not cancel: the peaks are the true extremes over all
    // channels.
    CHECK(out[0].min == -1);
    CHECK(out[0].max == 1);
}
TEST_CASE("Sixty seconds of stereo decode and peak queries") {
    TempDirectory temp;
    std::vector<float> samples(60 * 48000 * 2);
    for (std::size_t i = 0; i < samples.size(); ++i)
        samples[i] = (i % 128) / 128.0f;
    wav(temp.path / "long.wav", 48000, 2, samples, false);
    auto start = std::chrono::steady_clock::now();
    auto loaded = wavy::loadAudioFile(temp.path / "long.wav", 0);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->frames() == 60 * 48000);
    wavy::PeakPyramid peaks(*loaded);
    const double decode = milliseconds(start);
    std::vector<wavy::PeakPair> out;
    out.reserve(1000);
    const auto* storage = out.data();
    start = std::chrono::steady_clock::now();
    for (int i = 0; i < 10000; ++i)
        peaks.query(i, loaded->frames() - i, 1000, out);
    const double queries = milliseconds(start);
    CHECK(out.size() == 1000);
    CHECK(out.data() == storage);
    wavy::log::info("audio-file", "decode + peaks: {:.2f} ms; 10000 queries: {:.2f} ms", decode,
                    queries);
#ifdef NDEBUG
    CHECK(decode < 1500);
    CHECK(queries < 250);
#endif
}
