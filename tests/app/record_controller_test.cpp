#include "EngineController.hpp"
#include "RecordController.hpp"
#include "TimelineModel.hpp"
#include "io/AudioFile.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <doctest/doctest.h>
#include <numbers>

namespace {
bool waitFor(const auto& predicate) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 3000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return predicate();
}
} // namespace

TEST_CASE("record controller validates arming and input without opening a device on construction") {
    QTemporaryDir directory;
    TimelineModel model;
    EngineController audio(model, nullptr, wavy::AudioEngine::DeviceMode::NoDevice);
    RecordController recorder(model, audio, nullptr, false, directory.path());
    CHECK_FALSE(audio.isRunning());
    recorder.startRecording();
    CHECK(recorder.lastError() == "Arm a track first");
    recorder.armTrack(1, true);
    recorder.startRecording();
    REQUIRE(waitFor([&] { return !recorder.busy(); }));
    CHECK(recorder.lastError() == "No input device");
    CHECK_FALSE(recorder.recording());
    recorder.armTrack(2, true);
    CHECK(recorder.armedTrackId() == 2);
    recorder.armTrack(1, false);
    CHECK(recorder.armedTrackId() == 2);
    recorder.armTrack(2, false);
    CHECK(recorder.armedTrackId() == -1);
}

TEST_CASE("offline takes land at compensated punch in with samples and one history step") {
    QTemporaryDir directory;
    TimelineModel model;
    model.addTrack("CON");
    const int track = model.trackIdAt(4);
    EngineController audio(model, nullptr, wavy::AudioEngine::DeviceMode::NoDevice);
    RecordController recorder(model, audio, nullptr, true, directory.path());
    recorder.armTrack(track, true);
    std::vector<float> samples(512 * 2);
    for (size_t i = 0; i < 512; ++i)
        samples[i * 2] = samples[i * 2 + 1] =
            float(0.25 * std::sin(2 * std::numbers::pi * 440 * i / 48000));
    QString firstPath;
    for (int take = 0; take < 2; ++take) {
        // Like a user: stop playback (its declick ramp finishes), place the playhead, then record.
        audio.pause();
        std::vector<float> silence(512 * 2);
        audio.engine().renderOffline(silence.data(), 512);
        audio.seek(1000);
        audio.engine().recorder().setInputLatencyFrames(12);
        recorder.startRecording();
        REQUIRE(waitFor([&] { return !recorder.busy(); }));
        REQUIRE(recorder.recording());
        recorder.armTrack(1, true);
        CHECK(recorder.armedTrackId() == track);
        audio.engine().feedInputOffline(samples.data(), 512);
        REQUIRE(waitFor([&] { return recorder.inputPeak() > 0.2f; }));
        recorder.stopRecording();
        REQUIRE(waitFor([&] { return !recorder.busy(); }));
        CHECK_FALSE(recorder.recording());
        REQUIRE(model.timeline().tracks().back().clips.size() == size_t(take + 1));
        const auto clip = model.timeline().tracks().back().clips.back();
        CHECK(clip.start == 988);
        CHECK(clip.length == 512);
        CHECK(audio.positionFrames() == 1500);
        const auto path = QString::fromStdString(clip.source);
        CHECK(QFileInfo::exists(path));
        CHECK(QFileInfo(path).fileName() == QString("_CON-take00%1.wav").arg(take + 1));
        if (take == 0)
            firstPath = path;
        else
            CHECK(path != firstPath);
        auto decoded = wavy::loadAudioFile(clip.source, 48000);
        REQUIRE(decoded);
        REQUIRE(decoded->samples.size() == samples.size());
        for (size_t i = 0; i < samples.size(); ++i)
            CHECK(decoded->samples[i] == doctest::Approx(samples[i]).epsilon(0.00001));
        model.undo();
        CHECK(model.timeline().tracks().back().clips.size() == size_t(take));
        model.redo();
        CHECK(model.timeline().tracks().back().clips.size() == size_t(take + 1));
    }
}

TEST_CASE("discontinuous input warns and keeps the accepted prefix") {
    QTemporaryDir directory;
    TimelineModel model;
    model.addTrack("Input");
    EngineController audio(model, nullptr, wavy::AudioEngine::DeviceMode::NoDevice);
    RecordController recorder(model, audio, nullptr, true, directory.path());
    recorder.armTrack(5, true);
    recorder.startRecording();
    REQUIRE(waitFor([&] { return recorder.recording(); }));
    std::vector<float> samples(512, 0.25f);
    audio.engine().feedInputOffline(samples.data(), 64);
    audio.engine().mixer().transport().seek(1000);
    audio.engine().feedInputOffline(samples.data(), 512);
    recorder.stopRecording();
    REQUIRE(waitFor([&] { return !recorder.busy(); }));
    // A seek ramps the old audio down before the transport jumps, so the input stays contiguous
    // for that long; everything after the jump is dropped. The ramp-down lasts as long as the
    // ramp-up got to, which here is the 64 frames played before the seek (the full ramp is 3 ms).
    constexpr qint64 ramp = 64;
    CHECK(recorder.droppedFrames() == 512 - ramp);
    CHECK(recorder.lastError().contains("frames dropped"));
    REQUIRE(model.timeline().tracks().back().clips.size() == 1);
    CHECK(model.timeline().tracks().back().clips.front().length == 64 + ramp);
}

TEST_CASE("recording names are Windows safe") {
    CHECK(RecordController::safeTrackName("a<>:\"/\\|?*b. ") == "a_________b");
    CHECK(RecordController::safeTrackName("...") == "Track");
    CHECK(RecordController::safeTrackName("lpt1.txt") == "_lpt1.txt");
    CHECK(RecordController::safeTrackName("Vocals") == "Vocals");
}
