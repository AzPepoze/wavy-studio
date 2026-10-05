#include "AudioFile.hpp"
#define MA_NO_DEVICE_IO
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_THREADING
#define MA_NO_GENERATION
#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#include <memory>
#include <miniaudio.h>

namespace wavy {
std::expected<AudioBuffer, LoadError> decodeAudioFile(const std::filesystem::path& path,
                                                      unsigned targetSampleRate) {
    auto decoder = std::make_unique<ma_decoder>();
    const auto config = ma_decoder_config_init(ma_format_f32, 0, targetSampleRate);
#ifdef _WIN32
    const auto status = ma_decoder_init_file_w(path.c_str(), &config, decoder.get());
#else
    const auto status = ma_decoder_init_file(path.c_str(), &config, decoder.get());
#endif
    if (status != MA_SUCCESS)
        return std::unexpected(status == MA_INVALID_FILE || status == MA_FORMAT_NOT_SUPPORTED
                                   ? LoadError::UnsupportedFormat
                                   : LoadError::DecodeFailed);
    struct Cleanup {
        ma_decoder* decoder;
        ~Cleanup() { ma_decoder_uninit(decoder); }
    } cleanup{decoder.get()};
    AudioBuffer buffer{decoder->outputSampleRate, decoder->outputChannels, {}};
    constexpr ma_uint64 chunkFrames = 4096;
    ma_uint64 length = 0;
    if (ma_decoder_get_length_in_pcm_frames(decoder.get(), &length) == MA_SUCCESS &&
        length <= buffer.samples.max_size() / buffer.channels - chunkFrames)
        buffer.samples.reserve(static_cast<std::size_t>(length + chunkFrames) * buffer.channels);
    while (true) {
        const auto offset = buffer.samples.size();
        buffer.samples.resize(offset + chunkFrames * buffer.channels);
        ma_uint64 read = 0;
        const auto result = ma_decoder_read_pcm_frames(
            decoder.get(), buffer.samples.data() + offset, chunkFrames, &read);
        buffer.samples.resize(offset + read * buffer.channels);
        if (result != MA_SUCCESS && result != MA_AT_END)
            return std::unexpected(LoadError::DecodeFailed);
        if (read == 0 || result == MA_AT_END)
            break;
    }
    return buffer;
}
} // namespace wavy
