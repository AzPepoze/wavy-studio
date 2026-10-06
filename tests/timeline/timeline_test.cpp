#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "core/Log.hpp"
#include "timeline/CompoundCommand.hpp"
#include "timeline/History.hpp"
#include <algorithm>
#include <chrono>
#include <doctest/doctest.h>
#include <limits>
#include <random>
#include <unordered_set>

using namespace wavy::timeline;
namespace {
TrackId addTrack(Timeline& timeline) {
    AddTrack command("track");
    command.apply(timeline);
    return command.trackId();
}
ClipId addClip(Timeline& timeline, TrackId track, Frames start = 10, Frames length = 20) {
    AddClip command(track, {{}, "audio", start, 5, length, 0.8f, 4, 6});
    REQUIRE(command.validate(timeline));
    command.apply(timeline);
    return command.createdClipId();
}
void invariant(const Timeline& timeline) {
    std::unordered_set<TrackId> tracks;
    std::unordered_set<ClipId> clips;
    for (const auto& track : timeline.tracks()) {
        CHECK(tracks.insert(track.id).second);
        CHECK(std::is_sorted(track.clips.begin(), track.clips.end(),
                             [](const Clip& a, const Clip& b) {
                                 return a.start == b.start ? a.id < b.id : a.start < b.start;
                             }));
        for (const auto& clip : track.clips) {
            CHECK(clips.insert(clip.id).second);
            CHECK(clip.length > 0);
            CHECK(clip.start >= 0);
            CHECK(clip.sourceOffset >= 0);
            CHECK(clip.fadeIn >= 0);
            CHECK(clip.fadeOut >= 0);
            CHECK(clip.fadeIn <= clip.length);
            CHECK(clip.fadeOut <= clip.length);
        }
    }
}
void roundTrip(Timeline& timeline, Command& command) {
    const auto before = timeline;
    REQUIRE(command.validate(timeline));
    command.apply(timeline);
    invariant(timeline);
    const auto after = timeline;
    command.revert(timeline);
    CHECK(timeline == before);
    command.apply(timeline);
    CHECK(timeline == after);
    command.revert(timeline);
    CHECK(timeline == before);
}
} // namespace
TEST_CASE("Every edit restores exact state and replays the same ids") {
    Timeline timeline;
    const auto a = addTrack(timeline), b = addTrack(timeline);
    const auto clip = addClip(timeline, a);
    addClip(timeline, a, 10);
    addClip(timeline, b, 3);
    AddTrack add("new");
    roundTrip(timeline, add);
    RemoveTrack remove(a);
    roundTrip(timeline, remove);
    MoveTrack reorder(a, 1);
    roundTrip(timeline, reorder);
    AddClip insert(a, {{}, "other", 2, 0, 7});
    roundTrip(timeline, insert);
    RemoveClip erase(clip);
    roundTrip(timeline, erase);
    MoveClip move(clip, b, 3);
    roundTrip(timeline, move);
    MoveClip same(clip, a, 50);
    roundTrip(timeline, same);
    SplitClip split(clip, 20);
    roundTrip(timeline, split);
    TrimClip left(clip, Edge::Left, 14);
    roundTrip(timeline, left);
    TrimClip right(clip, Edge::Right, 25);
    roundTrip(timeline, right);
    DuplicateClip duplicate(clip);
    roundTrip(timeline, duplicate);
    DuplicateClip overlapping(clip, 10);
    roundTrip(timeline, overlapping);
    SetClipGain gain(clip, 0.5f);
    roundTrip(timeline, gain);
    SetTrackGain trackGain(a, 0.4f);
    roundTrip(timeline, trackGain);
    SetTrackMute mute(a, true);
    roundTrip(timeline, mute);
    SetTrackSolo solo(a, true);
    roundTrip(timeline, solo);
}
TEST_CASE("Compound edits validate sequential state and restore ids and order") {
    Timeline timeline;
    const auto a = addTrack(timeline), b = addTrack(timeline);
    const auto clip = addClip(timeline, a);
    addClip(timeline, a, 10);
    addClip(timeline, b, 3);
    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<MoveTrack>(a, 1));
    commands.push_back(std::make_unique<MoveClip>(clip, b, 50));
    commands.push_back(std::make_unique<SplitClip>(clip, 60));
    commands.push_back(std::make_unique<AddClip>(a, Clip{{}, "new", 10, 0, 5}));
    CompoundCommand compound("edit", std::move(commands));
    CHECK(compound.name() == "edit");
    CHECK_FALSE(compound.mergeWith(compound));
    const auto before = timeline;
    REQUIRE(compound.validate(timeline));
    REQUIRE(compound.validate(timeline));
    CHECK(timeline == before);
    roundTrip(timeline, compound);
    AddClip next(a, {{}, "next", 0, 0, 1});
    next.apply(timeline);
    CHECK(next.createdClipId() == ClipId{6});
}
TEST_CASE("A failing middle compound step preserves timeline and both history branches") {
    Timeline timeline;
    const auto track = addTrack(timeline);
    const auto clip = addClip(timeline, track);
    History history(timeline);
    REQUIRE(history.execute(std::make_unique<SetTrackMute>(track, true)));
    REQUIRE(history.execute(std::make_unique<MoveClip>(clip, track, 50)));
    REQUIRE(history.undo());
    const auto before = timeline;
    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<RemoveClip>(clip));
    commands.push_back(std::make_unique<MoveClip>(clip, track, 0));
    commands.push_back(std::make_unique<SetTrackSolo>(track, true));
    CHECK(history.execute(std::make_unique<CompoundCommand>("bad", std::move(commands))).error() ==
          Error::UnknownClip);
    CHECK(timeline == before);
    CHECK(history.canUndo());
    CHECK(history.canRedo());
    REQUIRE(history.redo());
    CHECK(timeline.findClip(clip)->clip->start == 50);
    REQUIRE(history.undo());
    CHECK(timeline == before);
    REQUIRE(history.undo());
    CHECK_FALSE(timeline.findTrack(track)->muted);
    CHECK_FALSE(history.canUndo());
    std::vector<std::unique_ptr<Command>> allocating;
    allocating.push_back(std::make_unique<AddClip>(track, Clip{{}, "new", 0, 0, 1}));
    allocating.push_back(std::make_unique<RemoveTrack>(TrackId{99}));
    allocating.push_back(std::make_unique<SetTrackMute>(track, true));
    CHECK_FALSE(history.execute(std::make_unique<CompoundCommand>("bad", std::move(allocating))));
    AddClip next(track, {{}, "next", 0, 0, 1});
    next.apply(timeline);
    CHECK(next.createdClipId() == ClipId{2});
}
TEST_CASE("EditClip moves and trims as one undo redo step") {
    Timeline timeline;
    const auto a = addTrack(timeline), b = addTrack(timeline);
    const auto clip = addClip(timeline, a);
    addClip(timeline, a, 10);
    addClip(timeline, b, 50);
    const auto before = timeline;
    History history(timeline);
    REQUIRE(history.execute(std::make_unique<EditClip>(clip, b, 50, 8)));
    const auto location = timeline.findClip(clip);
    CHECK(location->track->id == b);
    CHECK(location->clip->start == 50);
    CHECK(location->clip->length == 8);
    CHECK(location->clip->sourceOffset == 5);
    const auto after = timeline;
    REQUIRE(history.undo());
    CHECK(timeline == before);
    CHECK_FALSE(history.canUndo());
    REQUIRE(history.redo());
    CHECK(timeline == after);
    CHECK_FALSE(history.canRedo());
    REQUIRE(history.undo());
    EditClip left(clip, a, 14, 16);
    roundTrip(timeline, left);
    left.apply(timeline);
    CHECK(timeline.findClip(clip)->clip->sourceOffset == 9);
    left.revert(timeline);
    EditClip right(clip, a, 10, 8);
    roundTrip(timeline, right);
    right.apply(timeline);
    CHECK(timeline.findClip(clip)->clip->start == 10);
    CHECK(timeline.findClip(clip)->clip->length == 8);
    CHECK(timeline.findClip(clip)->clip->sourceOffset == 5);
    right.revert(timeline);
    EditClip move(clip, b, 50, 20);
    roundTrip(timeline, move);
    move.apply(timeline);
    CHECK(timeline.findClip(clip)->track->id == b);
    CHECK(timeline.findClip(clip)->clip->start == 50);
    CHECK(timeline.findClip(clip)->clip->length == 20);
    CHECK(timeline.findClip(clip)->clip->sourceOffset == 5);
    move.revert(timeline);
    EditClip leftMove(clip, b, 14, 16);
    roundTrip(timeline, leftMove);
    EditClip unchanged(clip, a, 10, 20);
    roundTrip(timeline, unchanged);
}
TEST_CASE("EditClip rejects invalid ids and positions without changing history") {
    Timeline timeline;
    const auto track = addTrack(timeline);
    const auto clip = addClip(timeline, track);
    const auto before = timeline;
    History history(timeline);
    CHECK_FALSE(history.execute(std::make_unique<EditClip>(ClipId{99}, track, 0, 1)));
    CHECK_FALSE(history.execute(std::make_unique<EditClip>(clip, TrackId{99}, 0, 1)));
    CHECK_FALSE(history.execute(std::make_unique<EditClip>(clip, track, -1, 1)));
    CHECK_FALSE(history.execute(std::make_unique<EditClip>(clip, track, 0, 0)));
    CHECK_FALSE(history.execute(std::make_unique<EditClip>(clip, track, 0, -1)));
    CHECK_FALSE(history.execute(
        std::make_unique<EditClip>(clip, track, std::numeric_limits<Frames>::max(), 1)));
    CHECK_FALSE(history.execute(
        std::make_unique<EditClip>(clip, track, std::numeric_limits<Frames>::max() - 10, 1)));
    CHECK(timeline == before);
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.canRedo());
}
TEST_CASE("Validation leaves timeline and history unchanged") {
    Timeline timeline;
    const auto track = addTrack(timeline);
    const auto clip = addClip(timeline, track);
    const auto before = timeline;
    History history(timeline);
    for (const auto frame : {9, 10, 30, 31}) {
        CHECK_FALSE(history.execute(std::make_unique<SplitClip>(clip, frame)));
        CHECK(timeline == before);
        CHECK_FALSE(history.canUndo());
    }
    SplitClip first(clip, 11);
    roundTrip(timeline, first);
    SplitClip last(clip, 29);
    roundTrip(timeline, last);
    CHECK_FALSE(history.execute(std::make_unique<RemoveTrack>(TrackId{99})));
    CHECK_FALSE(history.execute(std::make_unique<RemoveClip>(ClipId{99})));
    CHECK_FALSE(history.execute(std::make_unique<MoveClip>(clip, TrackId{99}, 0)));
    CHECK_FALSE(history.execute(std::make_unique<MoveClip>(clip, track, -1)));
    CHECK_FALSE(history.execute(std::make_unique<MoveTrack>(track, 1)));
    CHECK_FALSE(history.execute(std::make_unique<AddClip>(track, Clip{{}, "bad", 0, 0, 0})));
    CHECK_FALSE(history.execute(std::make_unique<AddClip>(track, Clip{{}, "bad", 0, -1, 1})));
    CHECK_FALSE(history.execute(std::make_unique<AddClip>(
        track, Clip{{}, "bad", std::numeric_limits<Frames>::max(), 0, 1})));
    CHECK_FALSE(history.execute(
        std::make_unique<SetClipGain>(clip, std::numeric_limits<float>::quiet_NaN())));
    CHECK_FALSE(history.execute(std::make_unique<SetTrackGain>(track, -1)));
    CHECK_FALSE(history.execute(nullptr));
    CHECK(timeline == before);
}
TEST_CASE("Split offsets and fades, clamped trim and overlapping range queries") {
    Timeline timeline;
    const auto track = addTrack(timeline);
    const auto id = addClip(timeline, track);
    const auto before = timeline;
    SplitClip split(id, 20);
    split.apply(timeline);
    const auto& pieces = timeline.tracks()[0].clips;
    CHECK(pieces[0].length == 10);
    CHECK(pieces[0].fadeIn == 4);
    CHECK(pieces[0].fadeOut == 0);
    CHECK(pieces[1].start == 20);
    CHECK(pieces[1].sourceOffset == 15);
    CHECK(pieces[1].length == 10);
    CHECK(pieces[1].fadeIn == 0);
    CHECK(pieces[1].fadeOut == 6);
    split.revert(timeline);
    TrimClip extend(id, Edge::Left, -100);
    extend.apply(timeline);
    CHECK(timeline.findClip(id)->clip->start == 5);
    CHECK(timeline.findClip(id)->clip->sourceOffset == 0);
    CHECK(timeline.findClip(id)->clip->length == 25);
    extend.revert(timeline);
    TrimClip shrink(id, Edge::Left, 100);
    shrink.apply(timeline);
    CHECK(timeline.findClip(id)->clip->length == 1);
    invariant(timeline);
    shrink.revert(timeline);
    TrimClip right(id, Edge::Right, -100);
    right.apply(timeline);
    CHECK(timeline.findClip(id)->clip->length == 1);
    right.revert(timeline);
    CHECK(timeline == before);
    addClip(timeline, track, 10);
    addClip(timeline, track, 0, 1000);
    CHECK(timeline.clipsInRange(track, 10, 11).size() == 3);
    CHECK(timeline.clipsInRange(track, 30, 31).size() == 1);
    CHECK(timeline.clipsInRange(track, 1000, 1001).empty());
    CHECK(timeline.clipsInRange(track, 10, 10).empty());
    CHECK(timeline.clipsInRange(TrackId{99}, 0, 100).empty());
    CHECK(timeline.endFrame() == 1000);
    MoveClip moved(id, track, 2000);
    moved.apply(timeline);
    CHECK(timeline.clipsInRange(track, 2000, 2001).size() == 1);
    moved.revert(timeline);
    CHECK(timeline.clipsInRange(track, 2000, 2001).empty());
}
TEST_CASE("History chains, branch clearing, bounds, merging and id lifetime") {
    Timeline timeline;
    const auto track = addTrack(timeline);
    const auto id = addClip(timeline, track);
    const auto initial = timeline;
    History history(timeline);
    REQUIRE(history.execute(std::make_unique<SetClipGain>(id, 0.4f)));
    REQUIRE(history.execute(std::make_unique<SetClipGain>(id, 0.2f)));
    REQUIRE(history.undo());
    CHECK(timeline == initial);
    CHECK_FALSE(history.canUndo());
    REQUIRE(history.redo());
    CHECK(timeline.findClip(id)->clip->gain == 0.2f);
    REQUIRE(history.execute(std::make_unique<SetTrackGain>(track, 0.3f)));
    REQUIRE(history.execute(std::make_unique<SetTrackGain>(track, 0.7f)));
    REQUIRE(history.execute(std::make_unique<SetTrackMute>(track, true)));
    const auto final = timeline;
    while (history.undo()) {
    }
    CHECK(timeline == initial);
    while (history.redo()) {
    }
    CHECK(timeline == final);
    REQUIRE(history.undo());
    CHECK_FALSE(history.execute(std::make_unique<RemoveClip>(ClipId{999})));
    CHECK(history.canRedo());
    REQUIRE(history.execute(std::make_unique<SetTrackSolo>(track, true)));
    CHECK_FALSE(history.canRedo());
    history.clear();
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.canRedo());
    History bounded(timeline, 2);
    REQUIRE(bounded.execute(std::make_unique<SetTrackMute>(track, false)));
    const auto retained = timeline;
    REQUIRE(bounded.execute(std::make_unique<SetTrackSolo>(track, false)));
    REQUIRE(bounded.execute(std::make_unique<MoveClip>(id, track, 50)));
    REQUIRE(bounded.undo());
    REQUIRE(bounded.undo());
    CHECK_FALSE(bounded.undo());
    CHECK(timeline == retained);
    History zero(timeline, 0);
    REQUIRE(zero.execute(std::make_unique<SetTrackMute>(track, true)));
    CHECK_FALSE(zero.canUndo());
    History ids(timeline);
    auto add = std::make_unique<AddClip>(track, Clip{{}, "a", 1, 0, 1});
    auto* raw = add.get();
    REQUIRE(ids.execute(std::move(add)));
    const auto oldId = raw->createdClipId();
    REQUIRE(ids.undo());
    auto replacement = std::make_unique<AddClip>(track, Clip{{}, "b", 1, 0, 1});
    raw = replacement.get();
    REQUIRE(ids.execute(std::move(replacement)));
    CHECK(raw->createdClipId() != oldId);
    AddTrack oldTrack("old");
    oldTrack.apply(timeline);
    oldTrack.revert(timeline);
    AddTrack newTrack("new");
    newTrack.apply(timeline);
    CHECK(oldTrack.trackId() != newTrack.trackId());
}
TEST_CASE("Seeded random edits and undo redo preserve invariants") {
    wavy::log::setLevel(wavy::log::Level::Warn);
    Timeline timeline;
    for (int i = 0; i < 4; ++i)
        addClip(timeline, addTrack(timeline));
    const auto initial = timeline;
    History history(timeline, 10000);
    std::mt19937 rng(71391);
    for (int i = 0; i < 4000; ++i) {
        if (rng() % 8 == 0)
            history.undo();
        else if (rng() % 8 == 0)
            history.redo();
        else {
            const auto track = timeline.tracks()[rng() % timeline.tracks().size()].id;
            std::vector<ClipId> ids;
            for (const auto& t : timeline.tracks())
                for (const auto& c : t.clips)
                    ids.push_back(c.id);
            const auto id = ids.empty() ? ClipId{999999} : ids[rng() % ids.size()];
            std::unique_ptr<Command> command;
            switch (rng() % 14) {
            case 0:
                command = std::make_unique<AddClip>(
                    track, Clip{{}, "random", Frames(rng() % 1000), 10, 50});
                break;
            case 1:
                command = std::make_unique<RemoveClip>(id);
                break;
            case 2:
                command = std::make_unique<MoveClip>(id, track, rng() % 1000);
                break;
            case 3:
                command = std::make_unique<SplitClip>(id, rng() % 1000);
                break;
            case 4:
                command = std::make_unique<TrimClip>(id, rng() % 2 ? Edge::Left : Edge::Right,
                                                     Frames(rng() % 1100) - 50);
                break;
            case 5:
                command = std::make_unique<DuplicateClip>(id);
                break;
            case 6:
                command = std::make_unique<SetClipGain>(id, float(rng() % 100) / 100);
                break;
            case 7:
                command = std::make_unique<SetTrackGain>(track, float(rng() % 100) / 100);
                break;
            case 8:
                command = std::make_unique<SetTrackMute>(track, rng() % 2);
                break;
            case 9:
                command = std::make_unique<SetTrackSolo>(track, rng() % 2);
                break;
            case 10:
                command = std::make_unique<MoveTrack>(track, rng() % timeline.tracks().size());
                break;
            case 11:
                command = std::make_unique<AddTrack>("random");
                break;
            case 12:
                command = std::make_unique<EditClip>(id, track, rng() % 1000, 1 + rng() % 100);
                break;
            default:
                command = timeline.tracks().size() > 1
                              ? std::unique_ptr<Command>(std::make_unique<RemoveTrack>(track))
                              : std::unique_ptr<Command>(std::make_unique<AddTrack>("random"));
            }
            const auto before = timeline;
            if (!history.execute(std::move(command)))
                CHECK(timeline == before);
        }
        invariant(timeline);
        const auto& track = timeline.tracks()[rng() % timeline.tracks().size()];
        const Frames begin = rng() % 1000;
        const auto actual = timeline.clipsInRange(track.id, begin, begin + 25);
        std::vector<const Clip*> expected;
        for (const auto& clip : track.clips)
            if (clip.start < begin + 25 && clip.start + clip.length > begin)
                expected.push_back(&clip);
        CHECK(actual == expected);
    }
    while (history.undo())
        invariant(timeline);
    CHECK(timeline == initial);
}
TEST_CASE("100000 clips support 10000 indexed range queries") {
    wavy::log::setLevel(wavy::log::Level::Warn);
    Timeline timeline;
    std::vector<TrackId> tracks;
    for (int t = 0; t < 100; ++t) {
        const auto track = addTrack(timeline);
        tracks.push_back(track);
        for (int c = 0; c < 1000; ++c) {
            AddClip command(track, {{}, "audio", c * 100, 0, 50});
            command.apply(timeline);
        }
    }
    const auto start = std::chrono::steady_clock::now();
    size_t found = 0;
    for (int i = 0; i < 10000; ++i)
        found +=
            timeline.clipsInRange(tracks[i % 100], (i % 1000) * 100, (i % 1000) * 100 + 1).size();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(found == 10000);
#ifdef NDEBUG
    CHECK(elapsed < std::chrono::milliseconds(250));
#else
    (void)elapsed;
#endif
}
