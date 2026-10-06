#include "WaveformBridge.hpp"
#include "WaveformGeometry.hpp"
#include "io/AudioFile.hpp"
#include "io/Peaks.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>
#include <functional>
#include <limits>
#include <numbers>

namespace {
bool waitUntil(const std::function<bool()>& predicate, int milliseconds = 5000) {
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < milliseconds) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return predicate();
}

wavy::AudioBuffer makeBuffer(std::int64_t frames, unsigned channels) {
    wavy::AudioBuffer buffer{48000, channels, {}};
    buffer.samples.resize(static_cast<std::size_t>(frames) * channels);
    for (std::int64_t frame = 0; frame < frames; ++frame) {
        const auto a = static_cast<float>(std::sin(2 * std::numbers::pi * frame / 97.0));
        const auto b =
            static_cast<float>(std::sin(2 * std::numbers::pi * frame / 13.0 + frame * 1e-4));
        for (unsigned channel = 0; channel < channels; ++channel)
            buffer.samples[static_cast<std::size_t>(frame) * channels + channel] =
                channel == 0 ? 0.6f * a : -0.4f * b;
    }
    return buffer;
}

wavy::PeakPair brute(const wavy::AudioBuffer& buffer, std::int64_t begin, std::int64_t end) {
    begin = std::clamp<std::int64_t>(begin, 0, buffer.frames());
    end = std::clamp<std::int64_t>(end, begin, buffer.frames());
    float low = std::numeric_limits<float>::infinity();
    float high = -low;
    for (std::int64_t frame = begin; frame < end; ++frame)
        for (unsigned channel = 0; channel < buffer.channels; ++channel) {
            const float sample =
                buffer.samples[static_cast<std::size_t>(frame) * buffer.channels + channel];
            low = std::min(low, sample);
            high = std::max(high, sample);
        }
    return {low, high};
}
} // namespace

TEST_CASE("waveform envelope is exact for pyramid-aligned buckets") {
    const auto buffer = makeBuffer(64 * 64, 2);
    const wavy::PeakPyramid peaks(buffer);
    const std::int64_t length = 64 * 8;
    const std::int64_t offset = 64;
    const double pixelsPerFrame = 1.0 / 64;
    wavy::WaveformEnvelope envelope;
    wavy::buildWaveformEnvelope(peaks, buffer.frames(), offset, length, pixelsPerFrame, 0,
                                length * pixelsPerFrame, 1.f, envelope);
    REQUIRE(envelope.count == 8);
    for (int i = 0; i < envelope.count; ++i) {
        const std::int64_t first = offset + i * 64;
        const auto expected = brute(buffer, first, first + 64);
        CHECK(envelope.columns[static_cast<std::size_t>(i)].min ==
              doctest::Approx(expected.min).epsilon(0.0001));
        CHECK(envelope.columns[static_cast<std::size_t>(i)].max ==
              doctest::Approx(expected.max).epsilon(0.0001));
    }
}

TEST_CASE("waveform envelope stays within pyramid bucket bounds at many zoom levels") {
    const auto buffer = makeBuffer(120000, 2);
    const wavy::PeakPyramid peaks(buffer);
    const std::int64_t offset = 12345;
    const std::int64_t length = 90000;
    for (const double pixelsPerFrame : {0.003, 0.01, 0.05, 0.5, 1.0, 2.0, 7.0}) {
        const double width = length * pixelsPerFrame;
        wavy::WaveformEnvelope envelope;
        wavy::buildWaveformEnvelope(peaks, buffer.frames(), offset, length, pixelsPerFrame, 0,
                                    width, 1.f, envelope);
        REQUIRE(envelope.count > 0);
        const double bucket =
            static_cast<double>(envelope.endFrame - envelope.beginFrame) / envelope.count;
        for (int i = 0; i < envelope.count; ++i) {
            const auto first =
                envelope.beginFrame + static_cast<std::int64_t>(std::floor(i * bucket));
            const auto last = std::min(
                envelope.endFrame,
                std::max(first + 1, envelope.beginFrame +
                                        static_cast<std::int64_t>(std::floor((i + 1) * bucket))));
            const auto expected = brute(buffer, first, last);
            CHECK(envelope.columns[static_cast<std::size_t>(i)].min <= expected.min + 1e-5f);
            CHECK(envelope.columns[static_cast<std::size_t>(i)].max >= expected.max - 1e-5f);
        }
    }
}

TEST_CASE("waveform envelope handles empty, partial, and sub-pixel ranges") {
    const auto buffer = makeBuffer(10000, 1);
    const wavy::PeakPyramid peaks(buffer);
    wavy::WaveformEnvelope envelope;

    wavy::buildWaveformEnvelope(peaks, buffer.frames(), buffer.frames() + 100, 1000, 1.0, 0, 1000,
                                1.f, envelope);
    CHECK(envelope.count == 0);

    wavy::buildWaveformEnvelope(peaks, buffer.frames(), 0, buffer.frames() + 5000, 0.5, 0,
                                (buffer.frames() + 5000) * 0.5, 1.f, envelope);
    REQUIRE(envelope.count > 0);
    CHECK(envelope.firstX + envelope.count * envelope.stepX <= buffer.frames() * 0.5 + 1e-6);

    wavy::buildWaveformEnvelope(peaks, buffer.frames(), 0, 1000, 1.0, 50, 50, 1.f, envelope);
    CHECK(envelope.count == 0);

    wavy::buildWaveformEnvelope(peaks, buffer.frames(), 0, 1000, 1.0, 50, 51, 1.f, envelope);
    CHECK(envelope.count == 1);

    wavy::buildWaveformEnvelope(peaks, buffer.frames(), 0, 1000, 0.0, 0, 100, 1.f, envelope);
    CHECK(envelope.count == 0);
}

TEST_CASE("waveform envelope scales by amplitude") {
    const auto buffer = makeBuffer(50000, 2);
    const wavy::PeakPyramid peaks(buffer);
    wavy::WaveformEnvelope full;
    wavy::WaveformEnvelope half;
    wavy::WaveformEnvelope inverted;
    wavy::buildWaveformEnvelope(peaks, buffer.frames(), 7, 40000, 0.01, 0, 400, 1.f, full);
    wavy::buildWaveformEnvelope(peaks, buffer.frames(), 7, 40000, 0.01, 0, 400, 0.5f, half);
    wavy::buildWaveformEnvelope(peaks, buffer.frames(), 7, 40000, 0.01, 0, 400, -1.f, inverted);
    REQUIRE(full.count > 0);
    REQUIRE(half.count == full.count);
    for (int i = 0; i < full.count; ++i) {
        CHECK(half.columns[static_cast<std::size_t>(i)].min ==
              doctest::Approx(full.columns[static_cast<std::size_t>(i)].min * 0.5f));
        CHECK(half.columns[static_cast<std::size_t>(i)].max ==
              doctest::Approx(full.columns[static_cast<std::size_t>(i)].max * 0.5f));
        CHECK(inverted.columns[static_cast<std::size_t>(i)].min ==
              doctest::Approx(-full.columns[static_cast<std::size_t>(i)].max));
        CHECK(inverted.columns[static_cast<std::size_t>(i)].max ==
              doctest::Approx(-full.columns[static_cast<std::size_t>(i)].min));
    }
}

TEST_CASE("waveform data path reports peaks and refreshes when the source is ready") {
    wavy::SourceLibrary library(48000);
    WaveformBridge bridge;
    bridge.setLibrary(library);
    bridge.subscribe();
    bool ready = false;
    QString readyPath;
    QObject::connect(&bridge, &WaveformBridge::sourceReady, [&](const QString& path, bool ok) {
        if (ok) {
            ready = true;
            readyPath = path;
        }
    });
    const QString source = QStringLiteral("generated:sine:440:0.2:1");
    bridge.request(source);
    REQUIRE(waitUntil([&] { return ready; }));
    CHECK(readyPath == source);

    const auto data = library.get(source.toStdString());
    REQUIRE(data);
    REQUIRE(data->peaks);
    const auto frames = data->audio->frames();
    REQUIRE(frames > 0);
    wavy::WaveformEnvelope envelope;
    wavy::buildWaveformEnvelope(*data->peaks, frames, 0, frames, 200.0 / frames, 0, 200.0, 1.f,
                                envelope);
    REQUIRE(envelope.count > 0);
    bool nonFlat = false;
    for (const auto& column : envelope.columns)
        if (column.max > 0.1f || column.min < -0.1f)
            nonFlat = true;
    CHECK(nonFlat);
}
