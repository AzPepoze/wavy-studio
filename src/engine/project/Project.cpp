#include "project/Project.hpp"
#include "core/Log.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace wavy::project {
namespace {
namespace fs = std::filesystem;
using json = nlohmann::ordered_json;
using Frames = timeline::Frames;
using timeline::Clip;
using timeline::ClipId;
using timeline::FadeCurve;
using timeline::Track;
using timeline::TrackId;

std::function<bool()> commitHook;

ProjectError fail(ProjectError::Code code, std::string message) {
    return ProjectError{code, std::move(message)};
}

// JSON numbers are doubles, so a float such as 0.8f would print as 0.800000011920929. The shortest
// decimal that round-trips the value is readable and reads back as exactly the same value.
template <typename T> double readableFloat(T value) { return std::stod(std::format("{}", value)); }

bool writeDurable(const fs::path& path, std::string_view bytes, std::string& error) {
#ifdef _WIN32
    const int fd =
        _wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
#endif
    if (fd < 0) {
        error = "cannot open " + path.string() + " for writing: " + std::strerror(errno);
        return false;
    }
    std::size_t written = 0;
    bool ok = true;
    while (written < bytes.size()) {
#ifdef _WIN32
        const std::size_t chunk = std::min<std::size_t>(bytes.size() - written, 1u << 30);
        const auto count = _write(fd, bytes.data() + written, static_cast<unsigned>(chunk));
#else
        const auto count = ::write(fd, bytes.data() + written, bytes.size() - written);
#endif
        if (count <= 0) {
            error = "write failed on " + path.string() + ": " + std::strerror(errno);
            ok = false;
            break;
        }
        written += static_cast<std::size_t>(count);
    }
    if (ok) {
#ifdef _WIN32
        if (_commit(fd) != 0)
#else
        if (::fsync(fd) != 0)
#endif
        {
            error = "flush failed on " + path.string() + ": " + std::strerror(errno);
            ok = false;
        }
    }
#ifdef _WIN32
    _close(fd);
#else
    ::close(fd);
#endif
    return ok;
}

bool readFile(const fs::path& path, std::string& contents, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "cannot open " + path.string() + ": " + std::strerror(errno);
        return false;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (stream.bad()) {
        error = "read failed on " + path.string();
        return false;
    }
    contents = buffer.str();
    return true;
}

// Renames `from` over `to`, replacing an existing destination atomically.
bool replaceFile(const fs::path& from, const fs::path& to, std::string& error) {
#ifdef _WIN32
    if (fs::exists(to)) {
        if (!ReplaceFileW(to.c_str(), from.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS,
                          nullptr, nullptr)) {
            error =
                "ReplaceFileW failed (" + std::to_string(GetLastError()) + ") on " + to.string();
            return false;
        }
        return true;
    }
    if (!MoveFileExW(from.c_str(), to.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "MoveFileExW failed (" + std::to_string(GetLastError()) + ") on " + to.string();
        return false;
    }
    return true;
#else
    std::error_code code;
    fs::rename(from, to, code);
    if (code) {
        error = "rename to " + to.string() + " failed: " + code.message();
        return false;
    }
    return true;
#endif
}

void removeQuietly(const fs::path& path) {
    std::error_code code;
    fs::remove(path, code);
}

fs::path projectDirectory(const fs::path& path) {
    return path.has_parent_path() ? path.parent_path() : fs::path(".");
}

// Stored sources always use '/' so a project moves between Windows and Linux.
std::string genericPath(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

bool isGenerated(std::string_view source) { return source.rfind("generated:", 0) == 0; }

bool isAbsoluteGeneric(std::string_view value) {
    if (!value.empty() && value.front() == '/')
        return true;
    return value.size() >= 3 && std::isalpha(static_cast<unsigned char>(value[0])) &&
           value[1] == ':' && value[2] == '/';
}

bool escapesDirectory(std::string_view relative) {
    return relative == ".." || relative.rfind("../", 0) == 0;
}

// True when a generic path has a "." or ".." segment or a doubled slash, so the cheap string
// append is not enough and the path needs lexical normalization.
bool hasDotSegment(const std::string& path) {
    return path.rfind("./", 0) == 0 || path.rfind("../", 0) == 0 ||
           path.find("/./") != std::string::npos || path.find("/../") != std::string::npos ||
           path.ends_with("/.") || path.ends_with("/..") || path.find("//") != std::string::npos;
}

std::string storeSource(const std::string& source, const std::string& directory) {
    if (isGenerated(source))
        return source;
    const std::string path = genericPath(source);
    if (!isAbsoluteGeneric(path))
        return path;
    // Common case: the source already lives under the project folder, so strip the prefix. Fall
    // back to the filesystem for dot segments and roots it cannot relate.
    std::string base = directory;
    while (base.size() > 1 && base.back() == '/')
        base.pop_back();
    if (base != "." && !base.empty() && path.size() > base.size() + 1 &&
        path.compare(0, base.size(), base) == 0 && path[base.size()] == '/' &&
        !hasDotSegment(path)) {
        return path.substr(base.size() + 1);
    }
    const auto relative = fs::path(path).lexically_relative(directory);
    const auto text = relative.generic_string();
    if (relative.empty() || escapesDirectory(text))
        return path;
    return text;
}

std::string resolveSource(const std::string& stored, const std::string& directory) {
    if (isGenerated(stored))
        return stored;
    const std::string path = genericPath(stored);
    if (isAbsoluteGeneric(path))
        return path;
    std::string joined;
    if (directory.empty() || directory == ".") {
        joined = path;
    } else {
        joined = directory;
        if (joined.back() != '/')
            joined.push_back('/');
        joined += path;
    }
    if (hasDotSegment(joined))
        return fs::path(joined).lexically_normal().generic_string();
    return joined;
}

// ---- JSON reading helpers ---------------------------------------------------------------

const json* findField(const json& object, const char* key) {
    if (!object.is_object())
        return nullptr;
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &*it;
}

bool integerValue(const json& value, Frames& out) {
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number > static_cast<std::uint64_t>(std::numeric_limits<Frames>::max()))
            return false;
        out = static_cast<Frames>(number);
        return true;
    }
    if (value.is_number_integer()) {
        out = value.get<Frames>();
        return true;
    }
    return false;
}

bool unsignedValue(const json& value, std::uint32_t& out, bool allowZero) {
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if ((!allowZero && number == 0) || number > std::numeric_limits<std::uint32_t>::max())
            return false;
        out = static_cast<std::uint32_t>(number);
        return true;
    }
    if (value.is_number_integer()) {
        const auto number = value.get<std::int64_t>();
        if ((!allowZero && number <= 0) || number > std::numeric_limits<std::uint32_t>::max())
            return false;
        out = static_cast<std::uint32_t>(number);
        return true;
    }
    return false;
}

bool gainValue(const json& value, float& out) {
    if (!value.is_number())
        return false;
    const double number = value.get<double>();
    const float gain = static_cast<float>(number);
    if (!std::isfinite(number) || !std::isfinite(gain) || gain < 0.f)
        return false;
    out = gain;
    return true;
}

std::string_view fadeCurveName(FadeCurve curve) {
    switch (curve) {
    case FadeCurve::Linear:
        return "linear";
    case FadeCurve::EqualPower:
        return "equalPower";
    case FadeCurve::Exponential:
        return "exponential";
    }
    return "equalPower";
}

std::optional<FadeCurve> parseFadeCurve(std::string_view name) {
    if (name == "linear")
        return FadeCurve::Linear;
    if (name == "equalPower")
        return FadeCurve::EqualPower;
    if (name == "exponential")
        return FadeCurve::Exponential;
    return std::nullopt;
}

std::optional<ProjectError> optionalCurve(const json& object, const std::string& path,
                                          const char* key, FadeCurve& out) {
    const json* value = findField(object, key);
    if (!value)
        return std::nullopt;
    if (!value->is_string())
        return fail(ProjectError::Code::Validation, path + "." + key + ": expected a curve name");
    const auto curve = parseFadeCurve(value->get<std::string>());
    if (!curve)
        return fail(ProjectError::Code::Validation, path + "." + key + ": unknown fade curve \"" +
                                                        value->get<std::string>() + "\"");
    out = *curve;
    return std::nullopt;
}

bool validTimeSignature(unsigned numerator, unsigned denominator) {
    if (numerator < 1 || numerator > 64)
        return false;
    return denominator == 1 || denominator == 2 || denominator == 4 || denominator == 8 ||
           denominator == 16 || denominator == 32;
}

std::optional<ProjectError> requiredInteger(const json& object, const std::string& path,
                                            const char* key, Frames& out) {
    const json* value = findField(object, key);
    if (!value)
        return fail(ProjectError::Code::Validation,
                    path + ": missing required field \"" + key + "\"");
    if (!integerValue(*value, out))
        return fail(ProjectError::Code::Validation, path + "." + key + ": expected an integer");
    return std::nullopt;
}

std::optional<ProjectError> parseClip(const json& object, const std::string& path,
                                      const std::string& directory, Clip& out,
                                      std::unordered_set<std::uint32_t>& clipIds,
                                      std::optional<std::uint32_t> enclosingTrack) {
    if (!object.is_object())
        return fail(ProjectError::Code::Validation, path + ": expected an object");

    const json* id = findField(object, "id");
    std::uint32_t clipId = 0;
    if (!id || !unsignedValue(*id, clipId, false))
        return fail(ProjectError::Code::Validation, path + ".id: expected a positive integer id");
    if (!clipIds.insert(clipId).second)
        return fail(ProjectError::Code::Validation,
                    path + ".id: duplicate clip id " + std::to_string(clipId));
    out.id = ClipId{clipId};

    const json* source = findField(object, "source");
    if (!source || !source->is_string())
        return fail(ProjectError::Code::Validation, path + ".source: expected a string");
    out.source = resolveSource(source->get<std::string>(), directory);
    if (out.source.empty())
        return fail(ProjectError::Code::Validation, path + ".source: must not be empty");

    if (auto error = requiredInteger(object, path, "start", out.start))
        return error;
    if (auto error = requiredInteger(object, path, "sourceOffset", out.sourceOffset))
        return error;
    if (auto error = requiredInteger(object, path, "length", out.length))
        return error;
    if (auto error = requiredInteger(object, path, "fadeIn", out.fadeIn))
        return error;
    if (auto error = requiredInteger(object, path, "fadeOut", out.fadeOut))
        return error;

    const json* gain = findField(object, "gain");
    if (!gain)
        return fail(ProjectError::Code::Validation, path + ": missing required field \"gain\"");
    if (!gainValue(*gain, out.gain))
        return fail(ProjectError::Code::Validation,
                    path + ".gain: expected a finite non-negative number");

    if (const json* muted = findField(object, "muted")) {
        if (!muted->is_boolean())
            return fail(ProjectError::Code::Validation, path + ".muted: expected a boolean");
        out.muted = muted->get<bool>();
    }
    if (auto error = optionalCurve(object, path, "fadeInCurve", out.fadeInCurve))
        return error;
    if (auto error = optionalCurve(object, path, "fadeOutCurve", out.fadeOutCurve))
        return error;

    if (out.start < 0)
        return fail(ProjectError::Code::Validation, path + ".start: must not be negative");
    if (out.sourceOffset < 0)
        return fail(ProjectError::Code::Validation, path + ".sourceOffset: must not be negative");
    if (out.length <= 0)
        return fail(ProjectError::Code::Validation, path + ".length: must be positive");
    if (out.start > std::numeric_limits<Frames>::max() - out.length)
        return fail(ProjectError::Code::Validation, path + ".start: start + length overflows");
    if (out.sourceOffset > std::numeric_limits<Frames>::max() - out.length)
        return fail(ProjectError::Code::Validation,
                    path + ".sourceOffset: sourceOffset + length overflows");
    if (out.fadeIn < 0)
        return fail(ProjectError::Code::Validation, path + ".fadeIn: must not be negative");
    if (out.fadeOut < 0)
        return fail(ProjectError::Code::Validation, path + ".fadeOut: must not be negative");
    if (out.fadeIn > out.length)
        return fail(ProjectError::Code::Validation, path + ".fadeIn: fade is longer than the clip");
    if (out.fadeOut > out.length)
        return fail(ProjectError::Code::Validation,
                    path + ".fadeOut: fade is longer than the clip");

    if (enclosingTrack) {
        if (const json* declared = findField(object, "track")) {
            std::uint32_t trackId = 0;
            if (!unsignedValue(*declared, trackId, false) || trackId != *enclosingTrack)
                return fail(ProjectError::Code::Validation,
                            path + ".track: does not match the enclosing track");
        }
    }
    return std::nullopt;
}

// Fills `loaded`; returns the first error instead.
std::optional<ProjectError> parseDocument(const json& document, const fs::path& directory,
                                          LoadedProject& loaded) {
    if (!document.is_object())
        return fail(ProjectError::Code::InvalidFormat, "root: expected a JSON object");

    const std::string directoryText = genericPath(directory.generic_string());

    const json* format = findField(document, "format");
    if (!format)
        return fail(ProjectError::Code::InvalidFormat, "root: missing required field \"format\"");
    if (!format->is_string() || format->get<std::string>() != std::string(formatName))
        return fail(ProjectError::Code::InvalidFormat,
                    "root.format: expected \"" + std::string(formatName) + "\"");

    const json* version = findField(document, "version");
    if (!version)
        return fail(ProjectError::Code::InvalidFormat, "root: missing required field \"version\"");
    Frames versionValue = 0;
    if (!integerValue(*version, versionValue) || versionValue < 1 || versionValue > formatVersion)
        return fail(ProjectError::Code::UnsupportedVersion,
                    "root.version: project version is not supported by this build (expected " +
                        std::to_string(formatVersion) + ")");

    ProjectMeta meta;
    if (const json* appVersion = findField(document, "appVersion")) {
        if (!appVersion->is_string())
            return fail(ProjectError::Code::InvalidFormat, "root.appVersion: expected a string");
        meta.applicationVersion = appVersion->get<std::string>();
    }

    const json* sampleRate = findField(document, "sampleRate");
    Frames sampleRateValue = 0;
    if (!sampleRate || !integerValue(*sampleRate, sampleRateValue))
        return fail(ProjectError::Code::InvalidFormat, "root.sampleRate: expected an integer");
    if (sampleRateValue < 0 || sampleRateValue > std::numeric_limits<unsigned>::max())
        return fail(ProjectError::Code::Validation,
                    "root.sampleRate: must fit a 32-bit unsigned integer");

    const json* nextTrackJson = findField(document, "nextTrackId");
    std::uint32_t nextTrack = 0;
    if (!nextTrackJson || !unsignedValue(*nextTrackJson, nextTrack, true))
        return fail(ProjectError::Code::Validation,
                    "root.nextTrackId: expected an integer in [0, 4294967295]");
    const json* nextClipJson = findField(document, "nextClipId");
    std::uint32_t nextClip = 0;
    if (!nextClipJson || !unsignedValue(*nextClipJson, nextClip, true))
        return fail(ProjectError::Code::Validation,
                    "root.nextClipId: expected an integer in [0, 4294967295]");

    double tempoBpm = 120.0;
    if (const json* tempo = findField(document, "tempo")) {
        if (!tempo->is_number())
            return fail(ProjectError::Code::Validation, "root.tempo: expected a number");
        tempoBpm = tempo->get<double>();
        if (!std::isfinite(tempoBpm) || tempoBpm < 20.0 || tempoBpm > 999.0)
            return fail(ProjectError::Code::Validation,
                        "root.tempo: must be a finite number in [20, 999]");
    }
    unsigned numerator = 4, denominator = 4;
    if (const json* signature = findField(document, "timeSignature")) {
        if (!signature->is_array() || signature->size() != 2)
            return fail(ProjectError::Code::Validation,
                        "root.timeSignature: expected [numerator, denominator]");
        std::uint32_t parsedNumerator = 0, parsedDenominator = 0;
        if (!unsignedValue((*signature)[0], parsedNumerator, false) || parsedNumerator > 64)
            return fail(ProjectError::Code::Validation,
                        "root.timeSignature[0]: numerator must be an integer in [1, 64]");
        if (!unsignedValue((*signature)[1], parsedDenominator, false) ||
            !validTimeSignature(parsedNumerator, parsedDenominator))
            return fail(ProjectError::Code::Validation,
                        "root.timeSignature[1]: denominator must be one of 1, 2, 4, 8, 16, 32");
        numerator = parsedNumerator;
        denominator = parsedDenominator;
    }

    const json* tracksJson = findField(document, "tracks");
    if (!tracksJson || !tracksJson->is_array())
        return fail(ProjectError::Code::InvalidFormat, "root.tracks: expected an array");

    std::vector<Track> tracks;
    std::unordered_map<std::uint32_t, std::size_t> trackIndex;
    for (std::size_t i = 0; i < tracksJson->size(); ++i) {
        const std::string path = "tracks[" + std::to_string(i) + "]";
        const json& trackJson = (*tracksJson)[i];
        if (!trackJson.is_object())
            return fail(ProjectError::Code::Validation, path + ": expected an object");

        Track track;
        const json* id = findField(trackJson, "id");
        std::uint32_t trackId = 0;
        if (!id || !unsignedValue(*id, trackId, false))
            return fail(ProjectError::Code::Validation,
                        path + ".id: expected a positive integer id");
        if (!trackIndex.emplace(trackId, tracks.size()).second)
            return fail(ProjectError::Code::Validation,
                        path + ".id: duplicate track id " + std::to_string(trackId));
        track.id = TrackId{trackId};

        const json* name = findField(trackJson, "name");
        if (!name || !name->is_string())
            return fail(ProjectError::Code::Validation, path + ".name: expected a string");
        track.name = name->get<std::string>();

        const json* gain = findField(trackJson, "gain");
        if (!gain || !gainValue(*gain, track.gain))
            return fail(ProjectError::Code::Validation,
                        path + ".gain: expected a finite non-negative number");

        const json* muted = findField(trackJson, "muted");
        if (!muted || !muted->is_boolean())
            return fail(ProjectError::Code::Validation, path + ".muted: expected a boolean");
        track.muted = muted->get<bool>();

        const json* solo = findField(trackJson, "solo");
        if (!solo || !solo->is_boolean())
            return fail(ProjectError::Code::Validation, path + ".solo: expected a boolean");
        track.solo = solo->get<bool>();

        if (const json* extensions = findField(trackJson, "extensions");
            extensions && !extensions->is_object())
            return fail(ProjectError::Code::Validation, path + ".extensions: expected an object");
        tracks.push_back(std::move(track));
    }

    std::unordered_set<std::uint32_t> clipIds;
    for (std::size_t i = 0; i < tracksJson->size(); ++i) {
        const json& trackJson = (*tracksJson)[i];
        const json* clips = findField(trackJson, "clips");
        if (!clips)
            continue;
        if (!clips->is_array())
            return fail(ProjectError::Code::Validation,
                        "tracks[" + std::to_string(i) + "].clips: expected an array");
        tracks[i].clips.reserve(clips->size());
        for (std::size_t c = 0; c < clips->size(); ++c) {
            const std::string path =
                "tracks[" + std::to_string(i) + "].clips[" + std::to_string(c) + "]";
            Clip clip;
            if (auto error = parseClip((*clips)[c], path, directoryText, clip, clipIds,
                                       static_cast<std::uint32_t>(tracks[i].id.value)))
                return error;
            tracks[i].clips.push_back(std::move(clip));
        }
    }

    // A flat "clips" array with a "track" reference is accepted as well, so projects written by a
    // flat format still load; unknown references are rejected.
    if (const json* flat = findField(document, "clips")) {
        if (!flat->is_array())
            return fail(ProjectError::Code::Validation, "root.clips: expected an array");
        for (std::size_t c = 0; c < flat->size(); ++c) {
            const std::string path = "clips[" + std::to_string(c) + "]";
            const json& clipJson = (*flat)[c];
            const json* track = findField(clipJson, "track");
            std::uint32_t trackId = 0;
            if (!track || !unsignedValue(*track, trackId, false))
                return fail(ProjectError::Code::Validation,
                            path + ".track: expected a positive track id");
            const auto it = trackIndex.find(trackId);
            if (it == trackIndex.end())
                return fail(ProjectError::Code::Validation,
                            path + ".track: unknown track reference " + std::to_string(trackId) +
                                " (no such track)");
            Clip clip;
            if (auto error = parseClip(clipJson, path, directoryText, clip, clipIds, std::nullopt))
                return error;
            tracks[it->second].clips.push_back(std::move(clip));
        }
    }

    std::uint32_t maxTrackId = 0;
    for (const auto& entry : trackIndex)
        maxTrackId = std::max(maxTrackId, entry.first);
    std::uint32_t maxClipId = 0;
    for (const auto id : clipIds)
        maxClipId = std::max(maxClipId, id);
    if (nextTrack != 0 && nextTrack <= maxTrackId)
        return fail(ProjectError::Code::Validation,
                    "root.nextTrackId: " + std::to_string(nextTrack) +
                        " would reuse an existing track id");
    if (nextClip != 0 && nextClip <= maxClipId)
        return fail(ProjectError::Code::Validation, "root.nextClipId: " + std::to_string(nextClip) +
                                                        " would reuse an existing clip id");

    // Preserve top-level keys this build does not understand.
    static const std::set<std::string, std::less<>> known = {
        "format",        "version",     "appVersion", "sampleRate", "tempo",
        "timeSignature", "nextTrackId", "nextClipId", "tracks",     "clips"};
    for (const auto& [key, value] : document.items())
        if (!known.contains(key))
            meta.extensions[key] = value.dump();

    timeline::Timeline::Restorer restorer(static_cast<unsigned>(sampleRateValue));
    for (auto& track : tracks)
        restorer.addTrack(std::move(track));
    restorer.setCounters(TrackId{nextTrack}, ClipId{nextClip});
    restorer.setTempo(tempoBpm);
    restorer.setTimeSignature(numerator, denominator);
    loaded.timeline = restorer.build();
    loaded.meta = std::move(meta);
    return std::nullopt;
}

std::optional<ProjectError> validateForSave(const timeline::Timeline& timeline) {
    // The JSON paths are only materialized when a check fails, so a valid 100k-clip save does
    // not allocate one string per clip.
    const auto trackPath = [](std::size_t index) {
        return "tracks[" + std::to_string(index) + "]";
    };
    const auto clipPath = [](std::size_t track, std::size_t clip) {
        return "tracks[" + std::to_string(track) + "].clips[" + std::to_string(clip) + "]";
    };
    if (!std::isfinite(timeline.tempoBpm) || timeline.tempoBpm < 20.0 || timeline.tempoBpm > 999.0)
        return fail(ProjectError::Code::Validation,
                    "root.tempo: must be a finite number in [20, 999]");
    if (!validTimeSignature(timeline.timeSignatureNumerator, timeline.timeSignatureDenominator))
        return fail(ProjectError::Code::Validation,
                    "root.timeSignature: numerator in [1, 64] and denominator one of "
                    "1, 2, 4, 8, 16, 32");
    for (std::size_t t = 0; t < timeline.tracks().size(); ++t) {
        const Track& track = timeline.tracks()[t];
        if (track.id.value == 0)
            return fail(ProjectError::Code::Validation, trackPath(t) + ".id: must not be zero");
        if (!std::isfinite(track.gain) || track.gain < 0.f)
            return fail(ProjectError::Code::Validation,
                        trackPath(t) + ".gain: expected a finite non-negative number");
        for (std::size_t c = 0; c < track.clips.size(); ++c) {
            const Clip& clip = track.clips[c];
            if (clip.id.value == 0)
                return fail(ProjectError::Code::Validation,
                            clipPath(t, c) + ".id: must not be zero");
            if (clip.start < 0)
                return fail(ProjectError::Code::Validation,
                            clipPath(t, c) + ".start: must not be negative");
            if (clip.sourceOffset < 0)
                return fail(ProjectError::Code::Validation,
                            clipPath(t, c) + ".sourceOffset: must not be negative");
            if (clip.length <= 0)
                return fail(ProjectError::Code::Validation,
                            clipPath(t, c) + ".length: must be positive");
            if (clip.start > std::numeric_limits<Frames>::max() - clip.length)
                return fail(ProjectError::Code::Validation,
                            clipPath(t, c) + ".start: start + length overflows");
            if (clip.fadeIn < 0 || clip.fadeOut < 0 || clip.fadeIn > clip.length ||
                clip.fadeOut > clip.length)
                return fail(ProjectError::Code::Validation, clipPath(t, c) + ": invalid fade");
            if (!std::isfinite(clip.gain) || clip.gain < 0.f)
                return fail(ProjectError::Code::Validation,
                            clipPath(t, c) + ".gain: expected a finite non-negative number");
            if (!timeline::validFadeCurve(clip.fadeInCurve) ||
                !timeline::validFadeCurve(clip.fadeOutCurve))
                return fail(ProjectError::Code::Validation,
                            clipPath(t, c) + ".fadeInCurve: unknown fade curve");
        }
    }
    return std::nullopt;
}

json buildDocument(const timeline::Timeline& timeline, const ProjectMeta& meta,
                   const fs::path& directory) {
    const std::string directoryText = genericPath(directory.generic_string());
    json document = json::object();
    document["format"] = std::string(formatName);
    document["version"] = formatVersion;
    document["appVersion"] = meta.applicationVersion;
    document["sampleRate"] = timeline.sampleRate;
    document["tempo"] = readableFloat(timeline.tempoBpm);
    document["timeSignature"] =
        json::array({timeline.timeSignatureNumerator, timeline.timeSignatureDenominator});
    document["nextTrackId"] = timeline.nextTrackId().value;
    document["nextClipId"] = timeline.nextClipId().value;

    std::size_t clipCount = 0;
    for (const Track& track : timeline.tracks())
        clipCount += track.clips.size();
    json::array_t tracks;
    json::array_t clips;
    tracks.reserve(timeline.tracks().size());
    clips.reserve(clipCount);
    for (const Track& track : timeline.tracks()) {
        json trackObject = json::object();
        trackObject["id"] = track.id.value;
        trackObject["name"] = track.name;
        trackObject["gain"] = readableFloat(track.gain);
        trackObject["muted"] = track.muted;
        trackObject["solo"] = track.solo;
        trackObject["extensions"] = json::object();
        tracks.push_back(std::move(trackObject));

        for (const Clip& clip : track.clips) {
            json clipObject = json::object();
            clipObject["id"] = clip.id.value;
            clipObject["track"] = track.id.value;
            clipObject["source"] = storeSource(clip.source, directoryText);
            clipObject["start"] = clip.start;
            clipObject["sourceOffset"] = clip.sourceOffset;
            clipObject["length"] = clip.length;
            clipObject["gain"] = readableFloat(clip.gain);
            clipObject["fadeIn"] = clip.fadeIn;
            clipObject["fadeOut"] = clip.fadeOut;
            clipObject["muted"] = clip.muted;
            clipObject["fadeInCurve"] = std::string(fadeCurveName(clip.fadeInCurve));
            clipObject["fadeOutCurve"] = std::string(fadeCurveName(clip.fadeOutCurve));
            clips.push_back(std::move(clipObject));
        }
    }
    document["tracks"] = std::move(tracks);
    document["clips"] = std::move(clips);

    // Re-emit unknown top-level keys captured on load (never overwriting known ones).
    static const std::set<std::string, std::less<>> known = {
        "format",        "version",     "appVersion", "sampleRate", "tempo",
        "timeSignature", "nextTrackId", "nextClipId", "tracks",     "clips"};
    for (const auto& [key, text] : meta.extensions) {
        if (known.contains(key))
            continue;
        json value = json::parse(text, nullptr, false);
        if (value.is_discarded()) {
            log::warn("project", "ignoring unreadable extension \"{}\"", key);
            continue;
        }
        document[key] = std::move(value);
    }
    return document;
}
} // namespace

void setCommitHookForTesting(std::function<bool()> hook) { commitHook = std::move(hook); }

std::expected<void, ProjectError>
saveProject(const fs::path& path, const timeline::Timeline& timeline, const ProjectMeta& meta) {
    if (path.empty())
        return std::unexpected(fail(ProjectError::Code::FileWrite, "project path is empty"));
    if (auto error = validateForSave(timeline))
        return std::unexpected(std::move(*error));

    const json document = buildDocument(timeline, meta, projectDirectory(path));
    std::string text = document.dump(2, ' ', false, json::error_handler_t::replace);
    text.push_back('\n');

    const fs::path temp = path.string() + ".tmp";
    const fs::path backup = path.string() + ".bak";
    const fs::path backupTemp = path.string() + ".bak.tmp";

    std::string error;
    if (!writeDurable(temp, text, error)) {
        removeQuietly(temp);
        return std::unexpected(fail(ProjectError::Code::FileWrite, error));
    }

    // Test seam: fail here, before the existing project or its backup is touched.
    if (commitHook && !commitHook()) {
        removeQuietly(temp);
        return std::unexpected(
            fail(ProjectError::Code::FileWrite, "commit hook refused to replace the project"));
    }

    std::error_code code;
    if (fs::exists(path, code)) {
        std::string previous;
        if (!readFile(path, previous, error)) {
            removeQuietly(temp);
            return std::unexpected(fail(ProjectError::Code::FileWrite, error));
        }
        if (!writeDurable(backupTemp, previous, error)) {
            removeQuietly(temp);
            removeQuietly(backupTemp);
            return std::unexpected(fail(ProjectError::Code::FileWrite, error));
        }
        if (!replaceFile(backupTemp, backup, error)) {
            removeQuietly(temp);
            removeQuietly(backupTemp);
            return std::unexpected(fail(ProjectError::Code::FileWrite, error));
        }
    }

    if (!replaceFile(temp, path, error)) {
        removeQuietly(temp);
        return std::unexpected(fail(ProjectError::Code::FileWrite, error));
    }
    log::debug("project", "saved {}", path.string());
    return {};
}

std::expected<LoadedProject, ProjectError> loadProject(const fs::path& path) {
    std::string text;
    std::string error;
    if (!readFile(path, text, error))
        return std::unexpected(fail(ProjectError::Code::FileRead, path.string() + ": " + error));
    if (std::all_of(text.begin(), text.end(),
                    [](unsigned char character) { return std::isspace(character); }))
        return std::unexpected(
            fail(ProjectError::Code::Parse, path.string() + ": the project file is empty"));

    json document;
    try {
        document = json::parse(text);
    } catch (const json::exception& exception) {
        return std::unexpected(
            fail(ProjectError::Code::Parse, path.string() + ": invalid JSON: " + exception.what()));
    }

    LoadedProject loaded;
    if (auto parseError = parseDocument(document, projectDirectory(path), loaded))
        return std::unexpected(std::move(*parseError));
    log::debug("project", "loaded {}", path.string());
    return loaded;
}
} // namespace wavy::project
