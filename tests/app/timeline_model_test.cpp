#define DOCTEST_CONFIG_IMPLEMENT
#include "TimelineModel.hpp"
#include <QCoreApplication>
#include <doctest/doctest.h>

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
    auto clips = model.visibleClips(model.trackIdAt(0), 0, 3 * model.sampleRate());
    REQUIRE(clips.size() == 1);
    CHECK(clips[0].toMap().contains("color"));
    CHECK(clips[0].toMap().contains("clipId"));
    CHECK(model.visibleClips(model.trackIdAt(0), 10 * model.sampleRate(), 11 * model.sampleRate())
              .isEmpty());
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
