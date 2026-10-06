#include "EngineController.hpp"
#include "SnapshotPublisher.hpp"
#include "TimelineModel.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <algorithm>
#include <array>
#include <cmath>
#include <doctest/doctest.h>
#include <numbers>

namespace {
bool waitUntil(const auto& predicate, int milliseconds = 3000) {
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < milliseconds) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return predicate();
}
void settle() {
    QElapsedTimer clock;
    clock.start();
    waitUntil([&] { return clock.elapsed() >= 80; });
}
} // namespace

TEST_CASE("snapshot publication coalesces edits and follows history and track state") {
    TimelineModel model;
    wavy::Mixer mixer;
    SnapshotPublisher publisher(model, mixer);
    int publications = 0;
    QObject::connect(&publisher, &SnapshotPublisher::published, [&] { ++publications; });
    REQUIRE(waitUntil([&] { return publications >= 2; }));
    settle();
    const auto clip = model.visibleClips(1, 0, model.sampleRate()).first().toMap();
    const auto id = clip["clipId"].toInt();
    const auto length = clip["durationFrames"].toLongLong();
    const int before = publications;
    for (int i = 1; i <= 20; ++i)
        model.editClip(id, 1, i * 10, length);
    CHECK(publications == before);
    REQUIRE(waitUntil([&] { return publications > before; }));
    CHECK(publications == before + 1);
    model.undo();
    REQUIRE(waitUntil([&] { return publications > before + 1; }));
    model.redo();
    REQUIRE(waitUntil([&] { return publications > before + 2; }));
    model.setTrackState(0, "muted", true);
    REQUIRE(waitUntil([&] { return publications > before + 3; }));
}

TEST_CASE("offline playback synthesizes sine at clip offset and synchronizes transport") {
    TimelineModel model;
    model.addTrack("Test");
    model.setTrackState(4, "solo", true);
    model.addClip(5, "generated:sine:440:0.2:0.1", 23, 9600);
    EngineController audio(model, nullptr, wavy::AudioEngine::DeviceMode::NoDevice);
    SnapshotPublisher publisher(model, audio.engine().mixer());
    CHECK_FALSE(audio.isRunning());
    audio.play();
    CHECK(audio.isRunning());
    std::vector<float> output(4800 * 2);
    REQUIRE(waitUntil([&] {
        audio.seek(23);
        audio.engine().renderOffline(output.data(), 4800);
        return std::abs(output[2]) > 0.001;
    }));
    for (int i = 0; i < 4800; ++i) {
        const auto expected = 0.2 * std::sin(2 * std::numbers::pi * 440 * i / 48000.0);
        CHECK(output[i * 2] == doctest::Approx(expected).epsilon(0.00001));
        CHECK(output[i * 2 + 1] == output[i * 2]);
    }
    audio.seek(23 + 4800);
    audio.engine().renderOffline(output.data(), 64);
    CHECK(
        std::all_of(output.begin(), output.begin() + 128, [](float value) { return value == 0; }));
    audio.seek(100);
    audio.engine().renderOffline(output.data(), 64);
    CHECK(output[0] == doctest::Approx(0.2 * std::sin(2 * std::numbers::pi * 440 * 77 / 48000.0)));
    REQUIRE(waitUntil([&] { return model.playheadFrame() == 164; }));
    audio.pause();
    CHECK(audio.positionFrames() == 164);
    CHECK(audio.playbackState() == "Paused");
    audio.engine().renderOffline(output.data(), 64);
    CHECK(
        std::all_of(output.begin(), output.begin() + 128, [](float value) { return value == 0; }));
    CHECK(audio.positionFrames() == 164);
    model.setPlayheadFrame(400);
    CHECK(audio.positionFrames() == 400);
    audio.seek(-1);
    CHECK(model.playheadFrame() == 0);
    audio.setLoop(23, 87);
    CHECK(audio.loopEnabled());
    audio.play();
    audio.seek(23);
    audio.engine().renderOffline(output.data(), 128);
    CHECK(audio.positionFrames() == 23);
    audio.stop();
    CHECK_FALSE(audio.playing());
    CHECK(audio.positionFrames() == 0);
    CHECK(model.playheadFrame() == 0);
    CHECK(audio.playbackState() == "Stopped");
    CHECK(audio.isRunning());
}

TEST_CASE("large generated import returns promptly and missing sources remain silent") {
    TimelineModel model;
    wavy::Mixer mixer;
    SnapshotPublisher publisher(model, mixer);
    model.addTrack("Import");
    model.setTrackState(4, "solo", true);
    QElapsedTimer clock;
    clock.start();
    model.addClip(5, "generated:noise:100:0.2", 0, 48000LL * 600);
    CHECK(clock.elapsed() < 100);
    settle();
    model.action(model.visibleClips(5, 0, 1).first().toMap()["clipId"].toInt(), 5, "delete");
    model.addClip(5, "/tmp/wavy-playback-missing-source.wav", 0, 4800);
    model.addClip(5, "generated:unknown:440", 0, 4800);
    int publications = 0;
    QObject::connect(&publisher, &SnapshotPublisher::published, [&] { ++publications; });
    clock.restart();
    REQUIRE(waitUntil([&] { return publications >= 1; }));
    CHECK(clock.elapsed() < 100);
    REQUIRE(waitUntil([&] {
        return publisher.sourceLibrary().state("/tmp/wavy-playback-missing-source.wav") ==
                   wavy::SourceLibrary::State::Failed &&
               publisher.sourceLibrary().state("generated:unknown:440") ==
                   wavy::SourceLibrary::State::Failed;
    }));
    settle();
    mixer.transport().play();
    std::array<float, 128> output;
    mixer.render(output.data(), 64);
    CHECK(std::all_of(output.begin(), output.end(), [](float value) { return value == 0; }));
}

TEST_CASE("worker completion queues exactly one publication on the UI thread") {
    TimelineModel model;
    for (int row = 0; row < model.rowCount(); ++row) {
        const int track = model.trackIdAt(row);
        for (const auto& clip : model.visibleClips(track, 0, model.durationFrames()))
            model.action(clip.toMap()["clipId"].toInt(), track, "delete");
    }
    wavy::Mixer mixer;
    SnapshotPublisher publisher(model, mixer);
    int publications = 0;
    QObject::connect(&publisher, &SnapshotPublisher::published, [&] {
        CHECK(QThread::currentThread() == publisher.thread());
        ++publications;
    });
    REQUIRE(waitUntil([&] { return publications == 1; }));
    model.addClip(1, "generated:sine:440:0.2:0.1", 0, 9600);
    REQUIRE(waitUntil([&] { return publications == 2; }));
    publisher.sourceLibrary().waitIdle();
    CHECK(publications == 2);
    REQUIRE(publisher.sourceLibrary().get("generated:sine:440:0.2:0.1"));
    settle();
    CHECK(publications == 3);
    settle();
    CHECK(publications == 3);
}

TEST_CASE("publisher destruction joins in-flight loads and discards queued callbacks") {
    TimelineModel model;
    model.addClip(1, "generated:noise:100:0.2:60", 0, 48000LL * 60);
    wavy::Mixer mixer;
    for (int i = 0; i < 5; ++i) {
        auto publisher = std::make_unique<SnapshotPublisher>(model, mixer);
        bool published = false;
        QObject::connect(publisher.get(), &SnapshotPublisher::published, [&] { published = true; });
        REQUIRE(waitUntil([&] { return published; }));
        CHECK(publisher->sourceLibrary().state("generated:noise:100:0.2:60") ==
              wavy::SourceLibrary::State::Loading);
        if (i == 4)
            publisher->sourceLibrary().waitIdle();
        publisher.reset();
        settle();
    }
}
