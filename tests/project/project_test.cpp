#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "core/Log.hpp"
#include "project/Project.hpp"
#include "support/TimingLimit.hpp"
#include "timeline/Commands.hpp"
#include <algorithm>
#include <chrono>
#include <doctest/doctest.h>
#include <expected>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

using namespace wavy::project;
using namespace wavy::timeline;
namespace fs = std::filesystem;

namespace {
struct TempDirectory {
    fs::path path = fs::temp_directory_path() /
                    ("wavy-project-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TempDirectory() { fs::create_directories(path); }
    ~TempDirectory() {
        std::error_code error;
        fs::permissions(path, fs::perms::all, fs::perm_options::replace, error);
        fs::remove_all(path, error);
    }
};

std::string readText(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

void writeText(const fs::path& path, const std::string& text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}

std::expected<LoadedProject, ProjectError> loadText(const fs::path& path, const std::string& text) {
    writeText(path, text);
    return loadProject(path);
}

// A valid document independent of the save path, so tests can mutate one field at a time.
nlohmann::ordered_json validDocument() {
    using nlohmann::ordered_json;
    ordered_json clip = ordered_json::object();
    clip["id"] = 1;
    clip["source"] = "audio/a.wav";
    clip["start"] = 0;
    clip["sourceOffset"] = 0;
    clip["length"] = 100;
    clip["gain"] = 1.0;
    clip["fadeIn"] = 0;
    clip["fadeOut"] = 0;

    ordered_json clips = ordered_json::array();
    clips.push_back(std::move(clip));

    ordered_json track = ordered_json::object();
    track["id"] = 1;
    track["name"] = "A";
    track["gain"] = 1.0;
    track["muted"] = false;
    track["solo"] = false;
    track["extensions"] = ordered_json::object();
    track["clips"] = std::move(clips);

    ordered_json tracks = ordered_json::array();
    tracks.push_back(std::move(track));

    ordered_json document = ordered_json::object();
    document["format"] = "wavy-studio-project";
    document["version"] = 1;
    document["appVersion"] = "0.1.0";
    document["sampleRate"] = 48000;
    document["nextTrackId"] = 2;
    document["nextClipId"] = 2;
    document["tracks"] = std::move(tracks);
    return document;
}

Timeline sampleTimeline(const fs::path& directory) {
    Timeline timeline;
    timeline.sampleRate = 48000;
    AddTrack first("Drums");
    first.apply(timeline);
    AddClip kick(first.trackId(), {{}, "generated:sine", 0, 0, 100, 0.5f, 5, 10});
    kick.apply(timeline);
    AddClip snare(
        first.trackId(),
        {{}, (directory / "audio" / "snare.wav").generic_string(), 200, 10, 50, 0.8f, 2, 3});
    snare.apply(timeline);
    AddTrack second("Bass");
    second.apply(timeline);
    AddClip bass(
        second.trackId(),
        {{}, (directory / "audio" / "bass.wav").generic_string(), 50, 0, 200, 1.0f, 0, 20});
    bass.apply(timeline);
    return timeline;
}
} // namespace

TEST_CASE("round trip preserves data, ids and counters") {
    TempDirectory temp;
    const fs::path file = temp.path / "song.wavy";
    const auto source = sampleTimeline(temp.path);
    ProjectMeta meta;
    meta.applicationVersion = "0.1.0";
    REQUIRE(saveProject(file, source, meta));

    auto loaded = loadProject(file);
    REQUIRE(loaded);
    CHECK(loaded->timeline == source);
    CHECK(loaded->timeline.nextTrackId() == source.nextTrackId());
    CHECK(loaded->timeline.nextClipId() == source.nextClipId());
    CHECK(loaded->meta.applicationVersion == "0.1.0");

    const auto text = readText(file);
    CHECK(text.find("\"format\": \"wavy-studio-project\"") != std::string::npos);
    CHECK(text.find("\"version\": 1") != std::string::npos);

    // Floats are written as their shortest decimal, not as 0.800000011920929, and read back
    // exactly.
    Timeline::Restorer restorer;
    restorer.addTrack({TrackId{1}, "Bass", {}, 0.8f, false, false});
    const Timeline gains = restorer.build();
    const fs::path gainFile = temp.path / "gain.wavy";
    REQUIRE(saveProject(gainFile, gains, meta));
    CHECK(readText(gainFile).find("\"gain\": 0.8,") != std::string::npos);

    // A clip added after loading gets the next id, never one that existed before.
    AddClip fresh(loaded->timeline.tracks()[0].id, {{}, "audio/new.wav", 1000, 0, 10});
    REQUIRE(fresh.validate(loaded->timeline));
    fresh.apply(loaded->timeline);
    CHECK(fresh.createdClipId() == source.nextClipId());
    CHECK(loaded->timeline.nextClipId().value == source.nextClipId().value + 1);
}

TEST_CASE("unicode and spaces survive project and source paths") {
    TempDirectory temp;
    const fs::path directory = temp.path / "My Songs" / "café 空 格";
    fs::create_directories(directory);
    const fs::path file = directory / "prøject ünïcode.wavy";

    Timeline timeline;
    AddTrack track("Träcks 空");
    track.apply(timeline);
    const fs::path source = directory / "audio" / "löop ünïcode.wav";
    AddClip clip(track.trackId(), {{}, source.generic_string(), 0, 0, 10});
    clip.apply(timeline);

    REQUIRE(saveProject(file, timeline));
    auto loaded = loadProject(file);
    REQUIRE(loaded);
    CHECK(loaded->timeline == timeline);
    CHECK(loaded->timeline.tracks()[0].name == "Träcks 空");
    CHECK(loaded->timeline.tracks()[0].clips[0].source == source.generic_string());

    const auto document = nlohmann::ordered_json::parse(readText(file));
    REQUIRE(document.contains("clips"));
    CHECK(document["clips"][0]["source"] == "audio/löop ünïcode.wav");
    CHECK(document["clips"][0]["track"] == track.trackId().value);
    CHECK_FALSE(document["tracks"][0].contains("clips"));
}
TEST_CASE("sources are relative under the project and absolute or generated otherwise") {
    TempDirectory temp;
    const fs::path file = temp.path / "song.wavy";

    Timeline timeline;
    AddTrack track("A");
    track.apply(timeline);
    const std::vector<std::string> originals = {
        (temp.path / "audio" / "kick.wav").string(),
        temp.path.string() + "\\audio\\snare.wav",
        "/opt/samples/bass.wav",
        "C:\\Samples\\piano.wav",
        "generated:sine",
    };
    for (const auto& source : originals) {
        AddClip clip(track.trackId(), {{}, source, 0, 0, 10});
        clip.apply(timeline);
    }

    REQUIRE(saveProject(file, timeline));
    const auto document = nlohmann::ordered_json::parse(readText(file));
    const auto& stored = document["clips"];
    CHECK(stored[0]["source"] == "audio/kick.wav");
    CHECK(stored[1]["source"] == "audio/snare.wav");
    CHECK(stored[2]["source"] == "/opt/samples/bass.wav");
    CHECK(stored[3]["source"] == "C:/Samples/piano.wav");
    CHECK(stored[4]["source"] == "generated:sine");

    auto loaded = loadProject(file);
    REQUIRE(loaded);
    const auto& clips = loaded->timeline.tracks()[0].clips;
    CHECK(clips[0].source == (temp.path / "audio" / "kick.wav").generic_string());
    CHECK(clips[1].source == (temp.path / "audio" / "snare.wav").generic_string());
    CHECK(clips[2].source == "/opt/samples/bass.wav");
    CHECK(clips[3].source == "C:/Samples/piano.wav");
    CHECK(clips[4].source == "generated:sine");
}

TEST_CASE("malformed project files fail with a precise error") {
    TempDirectory temp;
    const fs::path path = temp.path / "bad.wavy";

    auto result = loadText(path, "");
    REQUIRE_FALSE(result);
    CHECK(result.error().code == ProjectError::Code::Parse);

    result = loadText(path, "{\"format\":\"wavy-studio-project\",\"version\":1,");
    REQUIRE_FALSE(result);
    CHECK(result.error().code == ProjectError::Code::Parse);

    result = loadText(path, "not json at all");
    REQUIRE_FALSE(result);
    CHECK(result.error().code == ProjectError::Code::Parse);

    result = loadText(path, "{\"format\":\"something-else\",\"version\":1}");
    REQUIRE_FALSE(result);
    CHECK(result.error().code == ProjectError::Code::InvalidFormat);

    result = loadText(path, "{\"version\":1}");
    REQUIRE_FALSE(result);
    CHECK(result.error().code == ProjectError::Code::InvalidFormat);
    CHECK(result.error().message.find("format") != std::string::npos);

    result = loadText(path, "{\"format\":\"wavy-studio-project\"}");
    REQUIRE_FALSE(result);
    CHECK(result.error().code == ProjectError::Code::InvalidFormat);
    CHECK(result.error().message.find("version") != std::string::npos);

    auto document = validDocument();
    document["version"] = 2;
    result = loadText(path, document.dump());
    REQUIRE_FALSE(result);
    CHECK(result.error().code == ProjectError::Code::UnsupportedVersion);
    CHECK(result.error().message.find("version") != std::string::npos);

    document = validDocument();
    document["tracks"][0]["clips"][0].erase("length");
    result = loadText(path, document.dump());
    REQUIRE_FALSE(result);
    CHECK(result.error().code == ProjectError::Code::Validation);
    CHECK(result.error().message.find("tracks[0].clips[0]") != std::string::npos);
    CHECK(result.error().message.find("length") != std::string::npos);
}

TEST_CASE("every load validation rule reports its JSON path") {
    TempDirectory temp;
    const fs::path path = temp.path / "bad.wavy";
    const auto expect = [&](nlohmann::ordered_json document, const std::string& fragment) {
        const auto result = loadText(path, document.dump());
        REQUIRE_FALSE(result);
        CHECK(result.error().code == ProjectError::Code::Validation);
        CHECK(result.error().message.find(fragment) != std::string::npos);
    };

    auto document = validDocument();
    document["tracks"][0]["clips"][0]["start"] = -1;
    expect(document, "start");

    document = validDocument();
    document["tracks"][0]["clips"][0]["sourceOffset"] = -5;
    expect(document, "sourceOffset");

    document = validDocument();
    document["tracks"][0]["clips"][0]["length"] = 0;
    expect(document, "length");

    document = validDocument();
    document["tracks"][0]["clips"][0]["length"] = -3;
    expect(document, "length");

    document = validDocument();
    document["tracks"][0]["clips"][0]["start"] = std::numeric_limits<std::int64_t>::max();
    document["tracks"][0]["clips"][0]["length"] = 2;
    expect(document, "overflow");

    document = validDocument();
    document["tracks"][0]["clips"][0]["sourceOffset"] = std::numeric_limits<std::int64_t>::max();
    document["tracks"][0]["clips"][0]["length"] = 2;
    expect(document, "overflow");

    document = validDocument();
    document["tracks"][0]["clips"][0]["fadeIn"] = 200;
    expect(document, "fadeIn");

    document = validDocument();
    document["tracks"][0]["clips"][0]["fadeOut"] = 200;
    expect(document, "fadeOut");

    document = validDocument();
    document["tracks"][0]["clips"][0]["gain"] = 1e39; // finite double, overflows float
    expect(document, "gain");

    document = validDocument();
    document["tracks"][0]["gain"] = -1.0;
    expect(document, "gain");

    document = validDocument();
    document["tracks"][0]["clips"][0]["gain"] = -0.5;
    expect(document, "gain");

    document = validDocument();
    document["tracks"][0]["clips"][0].erase("source");
    expect(document, "source");

    document = validDocument();
    document["tracks"][0].erase("name");
    expect(document, "name");

    document = validDocument();
    document["tracks"][0].erase("gain");
    expect(document, "gain");

    document = validDocument();
    document["tracks"][0].erase("muted");
    expect(document, "muted");

    document = validDocument();
    document["tracks"][0]["solo"] = "yes";
    expect(document, "solo");

    document = validDocument();
    auto duplicateTrack = document["tracks"][0];
    document["tracks"].push_back(duplicateTrack);
    expect(document, "duplicate track id");

    document = validDocument();
    auto duplicateClip = document["tracks"][0]["clips"][0];
    document["tracks"][0]["clips"].push_back(duplicateClip);
    expect(document, "duplicate clip id");

    document = validDocument();
    document["nextClipId"] = 1; // would collide with clip id 1
    expect(document, "reuse an existing clip id");

    document = validDocument();
    document["nextTrackId"] = 1; // would collide with track id 1
    expect(document, "reuse an existing track id");

    // A flat "clips" array referencing a missing track is the unknown-reference case.
    document = validDocument();
    auto flatClip = document["tracks"][0]["clips"][0];
    document["tracks"][0].erase("clips");
    flatClip["track"] = 99;
    document["clips"] = nlohmann::ordered_json::array({flatClip});
    expect(document, "unknown track reference");
}

TEST_CASE("a flat clips array with a known track reference loads") {
    TempDirectory temp;
    const fs::path path = temp.path / "flat.wavy";
    auto document = validDocument();
    auto flatClip = document["tracks"][0]["clips"][0];
    document["tracks"][0].erase("clips");
    flatClip["track"] = 1;
    document["clips"] = nlohmann::ordered_json::array({flatClip});

    const auto loaded = loadText(path, document.dump());
    REQUIRE(loaded);
    REQUIRE(loaded->timeline.tracks().size() == 1);
    REQUIRE(loaded->timeline.tracks()[0].clips.size() == 1);
    CHECK(loaded->timeline.tracks()[0].clips[0].id == ClipId{1});
}

TEST_CASE("a failed save leaves the previous project intact and no temp file") {
    TempDirectory temp;
    const fs::path path = temp.path / "song.wavy";
    REQUIRE(saveProject(path, sampleTimeline(temp.path)));
    const auto original = readText(path);

    auto changed = sampleTimeline(temp.path);
    changed.sampleRate = 44100;
    setCommitHookForTesting([] { return false; });
    const auto result = saveProject(path, changed);
    setCommitHookForTesting(nullptr);

    REQUIRE_FALSE(result);
    CHECK(result.error().code == ProjectError::Code::FileWrite);
    CHECK(readText(path) == original);
    CHECK_FALSE(fs::exists(path.string() + ".tmp"));
    CHECK_FALSE(fs::exists(path.string() + ".bak.tmp"));
    CHECK_FALSE(fs::exists(path.string() + ".bak")); // first save had no previous version
}

TEST_CASE("save keeps one backup of the previous version") {
    TempDirectory temp;
    const fs::path path = temp.path / "song.wavy";
    REQUIRE(saveProject(path, sampleTimeline(temp.path)));
    const auto firstText = readText(path);

    auto second = sampleTimeline(temp.path);
    second.sampleRate = 44100;
    REQUIRE(saveProject(path, second));

    REQUIRE(fs::exists(path.string() + ".bak"));
    CHECK(readText(path.string() + ".bak") == firstText);
    auto backup = loadProject(path.string() + ".bak");
    REQUIRE(backup);
    CHECK(backup->timeline.sampleRate == 48000);
    CHECK_FALSE(fs::exists(path.string() + ".tmp"));
    CHECK_FALSE(fs::exists(path.string() + ".bak.tmp"));
}

TEST_CASE("save into a read-only directory fails without touching the project") {
    TempDirectory temp;
    const fs::path directory = temp.path / "sub";
    fs::create_directories(directory);
    const fs::path path = directory / "song.wavy";
    REQUIRE(saveProject(path, sampleTimeline(temp.path)));
    const auto original = readText(path);

    std::error_code error;
    fs::permissions(directory, fs::perms::owner_read | fs::perms::owner_exec,
                    fs::perm_options::replace, error);
    auto result = saveProject(path, sampleTimeline(temp.path));
    fs::permissions(directory, fs::perms::all, fs::perm_options::replace, error);

    if (result) {
        MESSAGE("the filesystem ignored the read-only directory; skipping the assertion");
        return;
    }
    CHECK(result.error().code == ProjectError::Code::FileWrite);
    CHECK(readText(path) == original);
    CHECK_FALSE(fs::exists(path.string() + ".tmp"));
}

TEST_CASE("unknown keys are ignored on load and preserved on save") {
    TempDirectory temp;
    const fs::path path = temp.path / "song.wavy";
    auto document = validDocument();
    document["futureFeature"] = {{"mode", "x"}, {"count", 3}};

    auto loaded = loadText(path, document.dump());
    REQUIRE(loaded);
    CHECK(loaded->meta.extensions.count("futureFeature") == 1);

    REQUIRE(saveProject(path, loaded->timeline, loaded->meta));
    auto reloaded = loadProject(path);
    REQUIRE(reloaded);
    CHECK(reloaded->meta.extensions.count("futureFeature") == 1);

    const auto saved = nlohmann::ordered_json::parse(readText(path));
    REQUIRE(saved.contains("futureFeature"));
    CHECK(saved["futureFeature"]["mode"] == "x");
    CHECK(saved["futureFeature"]["count"] == 3);
}

TEST_CASE("100 tracks x 1000 clips saves and loads quickly") {
    wavy::log::setLevel(wavy::log::Level::Info);
    TempDirectory temp;
    const std::string source = (temp.path / "audio" / "loop.wav").generic_string();

    Timeline timeline;
    std::vector<TrackId> tracks;
    for (int t = 0; t < 100; ++t) {
        AddTrack track("Track " + std::to_string(t));
        track.apply(timeline);
        tracks.push_back(track.trackId());
        for (int c = 0; c < 1000; ++c) {
            AddClip clip(tracks.back(), {{}, source, Frames(c) * 100, 0, 50});
            clip.apply(timeline);
        }
    }

    // A wall-clock bound is fragile on a shared machine, so take the best of a few runs; a real
    // regression makes every run slow, while a scheduling spike only delays one.
    constexpr int runs = 3;
    double bestMilliseconds = std::numeric_limits<double>::max();
    for (int run = 0; run < runs; ++run) {
        const fs::path path = temp.path / ("big" + std::to_string(run) + ".wavy");
        const auto saveStart = std::chrono::steady_clock::now();
        REQUIRE(saveProject(path, timeline));
        const auto loadStart = std::chrono::steady_clock::now();
        auto loaded = loadProject(path);
        const auto end = std::chrono::steady_clock::now();
        REQUIRE(loaded);
        CHECK(loaded->timeline == timeline);
        const double saveMilliseconds =
            std::chrono::duration<double, std::milli>(loadStart - saveStart).count();
        const double loadMilliseconds =
            std::chrono::duration<double, std::milli>(end - loadStart).count();
        wavy::log::info("project-test", "run {} save {:.1f} ms, load {:.1f} ms, total {:.1f} ms",
                        run, saveMilliseconds, loadMilliseconds,
                        saveMilliseconds + loadMilliseconds);
        bestMilliseconds = std::min(bestMilliseconds, saveMilliseconds + loadMilliseconds);
    }
    wavy::log::info("project-test", "best of {} runs: {:.1f} ms", runs, bestMilliseconds);
#ifdef NDEBUG
    CHECK(bestMilliseconds < timingLimit(500.0));
#else
    CHECK(bestMilliseconds >= 0.0);
#endif
}

TEST_CASE("seeded random timelines round trip") {
    TempDirectory temp;
    std::mt19937 rng(20261006);
    for (int iteration = 0; iteration < 8; ++iteration) {
        Timeline timeline;
        const int trackCount = 1 + rng() % 6;
        for (int t = 0; t < trackCount; ++t) {
            AddTrack track("t" + std::to_string(t));
            track.apply(timeline);
            const int clipCount = rng() % 8;
            Frames cursor = 0;
            for (int c = 0; c < clipCount; ++c) {
                const Frames length = 1 + rng() % 200;
                const std::string source =
                    rng() % 2 ? (temp.path / "audio" /
                                 ("clip" + std::to_string(iteration) + "_" + std::to_string(t) +
                                  "_" + std::to_string(c) + ".wav"))
                                    .generic_string()
                              : "generated:test";
                AddClip clip(track.trackId(), {{},
                                               source,
                                               cursor,
                                               static_cast<Frames>(rng() % 50),
                                               length,
                                               float(rng() % 101) / 100.f,
                                               static_cast<Frames>(rng() % (length + 1)),
                                               static_cast<Frames>(rng() % (length + 1))});
                if (clip.validate(timeline))
                    clip.apply(timeline);
                cursor += length + rng() % 100;
            }
        }
        const fs::path path = temp.path / ("random" + std::to_string(iteration) + ".wavy");
        REQUIRE(saveProject(path, timeline));
        auto loaded = loadProject(path);
        REQUIRE(loaded);
        CHECK(loaded->timeline == timeline);
        CHECK(loaded->timeline.nextTrackId() == timeline.nextTrackId());
        CHECK(loaded->timeline.nextClipId() == timeline.nextClipId());
    }
}
