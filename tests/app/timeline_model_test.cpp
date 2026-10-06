#define DOCTEST_CONFIG_IMPLEMENT
#include "EngineController.hpp"
#include "SnapshotPublisher.hpp"
#include "TimelineModel.hpp"
#include "UserSettings.hpp"
#include "audio/Mixer.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>
#include <limits>
#include <vector>

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
float peakAmplitude(const std::vector<float>& samples) {
    float peak = 0.f;
    for (float sample : samples)
        peak = std::max(peak, std::abs(sample));
    return peak;
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}

TEST_CASE("timeline model exposes tracks and range filtered clips") {
    TimelineModel model;
    CHECK(model.rowCount() == 4);
    CHECK(model.tracks() == &model);
    CHECK(model.roleNames().value(TimelineModel::TrackIdRole) == "trackId");
    CHECK(model.roleNames().value(TimelineModel::NameRole) == "name");
    CHECK(model.roleNames().value(TimelineModel::MutedRole) == "muted");
    CHECK(model.roleNames().value(TimelineModel::SoloRole) == "solo");
    CHECK(model.roleNames().value(TimelineModel::GainRole) == "gain");
    auto clips = model.visibleClips(model.trackIdAt(0), 0, 3 * model.sampleRate());
    REQUIRE(clips.size() == 1);
    CHECK(clips[0].toMap().contains("trackIndex"));
    CHECK(clips[0].toMap().contains("clipId"));
    CHECK(model.visibleClips(model.trackIdAt(0), 10 * model.sampleRate(), 11 * model.sampleRate())
              .isEmpty());
}

TEST_CASE("clips expose a track palette index and a friendly display name") {
    TimelineModel model;
    for (const auto& clip : model.visibleClips(model.trackIdAt(0), 0, model.durationFrames())) {
        CHECK(clip.toMap().value("trackIndex").toInt() == 0);
        CHECK(clip.toMap().value("name").toString() == "Sine 110 Hz");
    }
    for (const auto& clip : model.visibleClips(model.trackIdAt(1), 0, model.durationFrames())) {
        CHECK(clip.toMap().value("trackIndex").toInt() == 1);
        CHECK(clip.toMap().value("name").toString() == "Sine 220 Hz");
    }
    const int track = model.trackIdAt(0);
    const qint64 end = model.durationFrames();
    model.addClip(track, "generated:noise:100:0.2:60", 0, 100);
    model.addClip(track, "/tmp/library/Kick Drum.wav", 0, 100);
    model.addClip(track, "/tmp/takes/20260101-120000-take001.wav", 0, 100);
    QSet<QString> names;
    for (const auto& clip : model.visibleClips(track, 0, end))
        names.insert(clip.toMap().value("name").toString());
    CHECK(names.contains("Noise"));
    CHECK(names.contains("Kick Drum"));
    CHECK(names.contains("20260101-120000-take001"));
}

TEST_CASE("track gain edits merge into one undo step and clamp") {
    TimelineModel model;
    const int track = model.trackIdAt(0);
    CHECK(model.data(model.index(0), TimelineModel::GainRole).toDouble() == doctest::Approx(0.0));
    model.setTrackGain(track, -6);
    model.setTrackGain(track, -12);
    model.setTrackGain(track, -3);
    CHECK(model.data(model.index(0), TimelineModel::GainRole).toDouble() == doctest::Approx(-3.0));
    CHECK(model.timeline().tracks()[0].gain == doctest::Approx(0.7079457f).epsilon(0.001));
    CHECK(model.canUndo());
    model.undo();
    CHECK(model.data(model.index(0), TimelineModel::GainRole).toDouble() == doctest::Approx(0.0));
    CHECK(model.timeline().tracks()[0].gain == doctest::Approx(1.0f));
    CHECK_FALSE(model.canUndo());
    model.redo();
    CHECK(model.data(model.index(0), TimelineModel::GainRole).toDouble() == doctest::Approx(-3.0));
    model.setTrackGain(track, -100);
    CHECK(model.data(model.index(0), TimelineModel::GainRole).toDouble() == doctest::Approx(-60.0));
    model.setTrackGain(track, 100);
    CHECK(model.data(model.index(0), TimelineModel::GainRole).toDouble() == doctest::Approx(6.0));
}

TEST_CASE("track gain republishes to the mixer and attenuates playback") {
    TimelineModel model;
    model.addTrack("Fader");
    model.setTrackState(4, "solo", true);
    model.addClip(5, "generated:sine:440:0.2:2", 0, 96000);
    EngineController audio(model, nullptr, wavy::AudioEngine::DeviceMode::NoDevice);
    SnapshotPublisher publisher(model, audio.engine().mixer());
    audio.play();
    std::vector<float> output(4800 * 2);
    REQUIRE(waitUntil([&] {
        audio.seek(0);
        audio.engine().renderOffline(output.data(), 4800);
        return peakAmplitude(output) > 0.15f;
    }));
    const float unityPeak = peakAmplitude(output);
    int publications = 0;
    QObject::connect(&publisher, &SnapshotPublisher::published, [&] { ++publications; });
    const int before = publications;
    model.setTrackGain(5, -6);
    REQUIRE(waitUntil([&] { return publications > before; }));
    REQUIRE(waitUntil([&] {
        audio.seek(0);
        audio.engine().renderOffline(output.data(), 4800);
        return std::abs(peakAmplitude(output) - unityPeak * 0.501f) < 0.03f;
    }));
    CHECK(model.data(model.index(4), TimelineModel::GainRole).toDouble() == doctest::Approx(-6.0));
}

TEST_CASE("edits, actions, and history update clips") {
    TimelineModel model;
    const int track = model.trackIdAt(0);
    const auto original = model.visibleClips(track, 0, 3 * model.sampleRate()).first().toMap();
    const int id = original.value("clipId").toInt();
    const auto duration = original.value("durationFrames").toLongLong();
    int clipSignals = 0;
    QObject::connect(&model, &TimelineModel::clipsChangedForTrack,
                     [&clipSignals](int) { ++clipSignals; });
    model.editClip(id, model.trackIdAt(1), model.sampleRate(), duration - 100);
    CHECK(model.visibleClips(track, 0, model.durationFrames()).size() == 2);
    auto movedClips = model.visibleClips(model.trackIdAt(1), 0, model.durationFrames());
    CHECK(movedClips.size() == 4);
    for (const auto& clip : movedClips)
        if (clip.toMap().value("clipId").toInt() == id)
            CHECK(clip.toMap().value("durationFrames").toLongLong() == duration - 100);
    model.undo();
    CHECK(model.visibleClips(track, 0, model.durationFrames()).size() == 3);
    for (const auto& clip : model.visibleClips(track, 0, model.durationFrames()))
        if (clip.toMap().value("clipId").toInt() == id)
            CHECK(clip.toMap().value("durationFrames").toLongLong() == duration);
    model.redo();
    CHECK(model.visibleClips(model.trackIdAt(1), 0, model.durationFrames()).size() == 4);
    CHECK(clipSignals > 0);

    int movedId = -1;
    for (const auto& item : model.visibleClips(model.trackIdAt(1), 0, model.durationFrames()))
        if (item.toMap().value("startFrame").toLongLong() == model.sampleRate())
            movedId = item.toMap().value("clipId").toInt();
    REQUIRE(movedId > 0);
    model.setPlayheadFrame(model.sampleRate() + duration / 2);
    model.action(movedId, model.trackIdAt(1), "split");
    CHECK(model.visibleClips(model.trackIdAt(1), 0, model.durationFrames()).size() == 5);
    model.undo();
    CHECK(model.visibleClips(model.trackIdAt(1), 0, model.durationFrames()).size() == 4);
    model.redo();
    CHECK(model.visibleClips(model.trackIdAt(1), 0, model.durationFrames()).size() == 5);
    model.undo();
    model.action(movedId, model.trackIdAt(1), "duplicate");
    CHECK(model.visibleClips(model.trackIdAt(1), 0, model.durationFrames()).size() == 5);
    model.undo();
    model.redo();
    CHECK(model.visibleClips(model.trackIdAt(1), 0, model.durationFrames()).size() == 5);
    model.undo();
    model.action(movedId, model.trackIdAt(1), "delete");
    CHECK(model.visibleClips(model.trackIdAt(1), 0, model.durationFrames()).size() == 3);
    model.undo();
    CHECK(model.visibleClips(model.trackIdAt(1), 0, model.durationFrames()).size() == 4);
    model.redo();
    CHECK(model.visibleClips(model.trackIdAt(1), 0, model.durationFrames()).size() == 3);
}

TEST_CASE("invalid ids are ignored and track notifications are emitted") {
    TimelineModel model;
    int changes = 0;
    QObject::connect(&model, &QAbstractItemModel::dataChanged, [&changes] { ++changes; });
    model.action(-1, 1, "delete");
    model.editClip(-1, 1, 0, 10);
    model.setTrackState(0, "muted", true);
    CHECK(changes == 1);
    CHECK(model.data(model.index(0), TimelineModel::MutedRole).toBool());
    model.undo();
    CHECK(!model.data(model.index(0), TimelineModel::MutedRole).toBool());
    int inserted = 0;
    QObject::connect(&model, &QAbstractItemModel::rowsInserted, [&inserted] { ++inserted; });
    model.addTrack("Extra");
    CHECK(inserted == 1);
    CHECK(model.rowCount() == 5);
    model.undo();
    CHECK(model.rowCount() == 4);
}

TEST_CASE("tempo and time signature setters notify and keep clips in place") {
    TimelineModel model;
    const auto clip =
        model.visibleClips(model.trackIdAt(0), 0, model.durationFrames()).first().toMap();
    int tempoChanges = 0;
    int signatureChanges = 0;
    QObject::connect(&model, &TimelineModel::tempoChanged, [&tempoChanges] { ++tempoChanges; });
    QObject::connect(&model, &TimelineModel::timeSignatureChanged,
                     [&signatureChanges] { ++signatureChanges; });
    CHECK(model.tempoBpm() == doctest::Approx(120.0));
    CHECK(model.framesPerBeat() == doctest::Approx(24000.0));
    model.setTempo(90);
    CHECK(model.tempoBpm() == doctest::Approx(90.0));
    CHECK(model.framesPerBeat() == doctest::Approx(32000.0));
    CHECK(tempoChanges == 1);
    // The tempo change must not move existing clips.
    const auto moved =
        model.visibleClips(model.trackIdAt(0), 0, model.durationFrames()).first().toMap();
    CHECK(moved.value("startFrame").toLongLong() == clip.value("startFrame").toLongLong());
    CHECK(moved.value("durationFrames").toLongLong() == clip.value("durationFrames").toLongLong());

    CHECK(model.timeSignatureNumerator() == 4);
    CHECK(model.timeSignatureDenominator() == 4);
    model.setTimeSignature(3, 4);
    CHECK(model.timeSignatureNumerator() == 3);
    CHECK(model.timeSignatureDenominator() == 4);
    CHECK(signatureChanges == 1);
}

TEST_CASE("tempo edits merge into one undo step and invalid values are rejected") {
    TimelineModel model;
    model.setTempo(100);
    model.setTempo(150);
    CHECK(model.tempoBpm() == doctest::Approx(150.0));
    CHECK(model.canUndo());
    model.undo();
    CHECK(model.tempoBpm() == doctest::Approx(120.0));
    CHECK_FALSE(model.canUndo());
    model.redo();
    CHECK(model.tempoBpm() == doctest::Approx(150.0));

    int tempoChanges = 0;
    QObject::connect(&model, &TimelineModel::tempoChanged, [&tempoChanges] { ++tempoChanges; });
    model.setTempo(19.9);
    model.setTempo(1000.0);
    model.setTempo(std::numeric_limits<double>::quiet_NaN());
    CHECK(model.tempoBpm() == doctest::Approx(150.0));
    CHECK(tempoChanges == 0);

    int signatureChanges = 0;
    QObject::connect(&model, &TimelineModel::timeSignatureChanged,
                     [&signatureChanges] { ++signatureChanges; });
    model.setTimeSignature(0, 4);
    model.setTimeSignature(65, 4);
    model.setTimeSignature(4, 3);
    CHECK(model.timeSignatureNumerator() == 4);
    CHECK(model.timeSignatureDenominator() == 4);
    CHECK(signatureChanges == 0);

    model.setTimeSignature(6, 8);
    CHECK(model.timeSignatureNumerator() == 6);
    CHECK(model.timeSignatureDenominator() == 8);
    CHECK(signatureChanges == 1);
    model.undo();
    CHECK(model.timeSignatureNumerator() == 4);
    CHECK(model.timeSignatureDenominator() == 4);
}

TEST_CASE("user settings round trip through an explicit ini file") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath("wavy.ini");
    {
        UserSettings settings(path);
        CHECK(settings.snapEnabled());
        CHECK(settings.snapDivision() == "auto");
        CHECK(settings.snapToClipEdges());
        CHECK(settings.snapToPlayhead());
        CHECK(settings.snapTolerancePixels() == 8);
        CHECK(settings.rulerMode() == "barsBeats");
        settings.setSnapEnabled(false);
        settings.setSnapDivision("1/16");
        settings.setSnapToClipEdges(false);
        settings.setSnapToPlayhead(false);
        settings.setSnapTolerancePixels(20);
        settings.setRulerMode("time");
    }
    UserSettings reloaded(path);
    CHECK_FALSE(reloaded.snapEnabled());
    CHECK(reloaded.snapDivision() == "1/16");
    CHECK_FALSE(reloaded.snapToClipEdges());
    CHECK_FALSE(reloaded.snapToPlayhead());
    CHECK(reloaded.snapTolerancePixels() == 20);
    CHECK(reloaded.rulerMode() == "time");
}

TEST_CASE("user settings honour the WAVY_SETTINGS_FILE override") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QByteArray path = dir.filePath("env.ini").toUtf8();
    qputenv("WAVY_SETTINGS_FILE", path);
    {
        UserSettings settings;
        settings.setSnapDivision("1/8");
        settings.setRulerMode("time");
    }
    {
        UserSettings settings;
        CHECK(settings.snapDivision() == "1/8");
        CHECK(settings.rulerMode() == "time");
    }
    qunsetenv("WAVY_SETTINGS_FILE");
}

TEST_CASE("user settings reject unknown values and clamp tolerance") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    UserSettings settings(dir.filePath("wavy.ini"));
    settings.setSnapDivision("not-a-division");
    CHECK(settings.snapDivision() == "auto");
    settings.setRulerMode("not-a-mode");
    CHECK(settings.rulerMode() == "barsBeats");
    settings.setSnapTolerancePixels(1000);
    CHECK(settings.snapTolerancePixels() == 64);
    settings.setSnapTolerancePixels(0);
    CHECK(settings.snapTolerancePixels() == 1);
    CHECK(UserSettings::validDivision("1/8T"));
    CHECK_FALSE(UserSettings::validDivision("1/64"));
    CHECK(UserSettings::validRulerMode("barsBeats"));
}
