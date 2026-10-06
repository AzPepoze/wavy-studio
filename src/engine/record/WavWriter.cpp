#include "record/WavWriter.hpp"
#include <bit>
#include <limits>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace wavy::record {
namespace {
constexpr unsigned headerSize = 58;
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
void put16(unsigned char* out, std::uint16_t value) {
    for (unsigned i = 0; i < 2; ++i)
        out[i] = static_cast<unsigned char>(value >> (8 * i));
}
void put32(unsigned char* out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        out[i] = static_cast<unsigned char>(value >> (8 * i));
}
} // namespace
WavWriter::~WavWriter() { close(); }
bool WavWriter::fail(const char* message) {
    error_ = message;
    return false;
}
bool WavWriter::open(const std::filesystem::path& path, unsigned channels, unsigned rate) {
    close();
    error_.clear();
    bytes_ = patched_ = 0;
    if ((channels != 1 && channels != 2) || !rate ||
        rate > std::numeric_limits<std::uint32_t>::max() / (channels * 4))
        return fail("Invalid WAV channels/sample rate");
    channels_ = channels;
    rate_ = rate;
#ifdef _WIN32
    file_ = _wfopen(path.c_str(), L"wbx");
#else
    file_ = std::fopen(path.c_str(), "wbx");
#endif
    if (!file_)
        return fail("Cannot create recording file (path exists or is not writable)");
    std::setvbuf(file_, buffer_.data(), _IOFBF, buffer_.size());
    unsigned char header[headerSize] = {
        'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 18, 0, 0,   0,
        3,   0,   0,   0,   0, 0, 0, 0, 0,   0,   0,   0,   0,   0,   32,  0,   0,  0, 'f', 'a',
        'c', 't', 4,   0,   0, 0, 0, 0, 0,   0,   'd', 'a', 't', 'a', 0,   0,   0,  0};
    put32(header + 4, headerSize - 8);
    put16(header + 22, channels);
    put32(header + 24, rate);
    put32(header + 28, rate * channels * 4);
    put16(header + 32, channels * 4);
    if (std::fwrite(header, 1, sizeof(header), file_) != sizeof(header))
        return fail("Cannot write WAV header");
    return patch();
}
bool WavWriter::write(const float* samples, std::size_t frames) {
    if (!file_ || !error_.empty())
        return false;
    // Stop before RIFF overflows, leaving the already written take recoverable.
    if (frames >
        (std::numeric_limits<std::uint32_t>::max() - (headerSize - 8) - bytes_) / (channels_ * 4))
        return fail("Recording reached the 4 GiB RIFF limit; start a new take");
    const auto count = frames * channels_;
    if constexpr (std::endian::native == std::endian::little) {
        const auto written = std::fwrite(samples, sizeof(float) * channels_, frames, file_);
        bytes_ += written * channels_ * 4;
        if (written != frames)
            return fail("Recording disk write failed");
    } else {
        for (std::size_t i = 0; i < count; ++i) {
            unsigned char sample[4];
            put32(sample, std::bit_cast<std::uint32_t>(samples[i]));
            if (std::fwrite(sample, 1, 4, file_) != 4)
                return fail("Recording disk write failed");
            bytes_ += 4;
        }
    }
    return bytes_ - patched_ < 1024 * 1024 || patch();
}
bool WavWriter::patch() {
    if (!file_)
        return false;
    // Flush audio before publishing its length, so a killed process leaves a valid prefix.
    if (std::fflush(file_) != 0)
        return fail("Cannot flush recording data");
    unsigned char size[4];
    put32(size, static_cast<std::uint32_t>(bytes_ + headerSize - 8));
    if (std::fseek(file_, 4, SEEK_SET) != 0 || std::fwrite(size, 1, 4, file_) != 4)
        return fail("Cannot patch WAV RIFF size");
    put32(size, static_cast<std::uint32_t>(frames()));
    if (std::fseek(file_, 46, SEEK_SET) != 0 || std::fwrite(size, 1, 4, file_) != 4)
        return fail("Cannot patch WAV fact sample count");
    put32(size, static_cast<std::uint32_t>(bytes_));
    if (std::fseek(file_, 54, SEEK_SET) != 0 || std::fwrite(size, 1, 4, file_) != 4 ||
        std::fflush(file_) != 0)
        return fail("Cannot patch WAV data size");
#ifdef _WIN32
    const auto synced = _commit(_fileno(file_));
    const auto seek = _fseeki64(file_, static_cast<__int64>(bytes_ + headerSize), SEEK_SET);
#else
    const auto synced = ::fsync(fileno(file_));
    const auto seek = ::fseeko(file_, static_cast<off_t>(bytes_ + headerSize), SEEK_SET);
#endif
    if (synced != 0 || seek != 0)
        return fail("Cannot sync recording file");
    patched_ = bytes_;
    return true;
}
bool WavWriter::close(bool finalize) {
    if (!file_)
        return error_.empty();
    const auto patched = !finalize || patch();
    const auto closed = std::fclose(file_) == 0;
    file_ = nullptr;
    if (!closed)
        fail("Cannot close recording file");
    return patched && closed && error_.empty();
}
} // namespace wavy::record
