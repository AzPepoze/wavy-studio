#pragma once
#include "timeline/Timeline.hpp"
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace wavy::project {
inline constexpr std::string_view formatName = "wavy-studio-project";
inline constexpr int formatVersion = 1;

struct ProjectError {
    enum class Code {
        FileRead,           // the project file could not be opened or read
        FileWrite,          // the temp file, backup or atomic rename step failed
        Parse,              // the file is empty or not valid JSON
        InvalidFormat,      // valid JSON but not a wavy-studio project document
        UnsupportedVersion, // the document's major version is newer (or older) than this build
        Validation,         // structurally valid, but a value violates a timeline invariant
    };
    Code code = Code::Parse;
    // Human readable; validation messages name the failing JSON path, for example
    // "tracks[0].clips[2].length: must be positive".
    std::string message;
};

// Supplied by the caller on save and returned on load. Deliberately JSON-free so the public
// engine headers stay dependency-light.
struct ProjectMeta {
    std::string applicationVersion; // written as "appVersion"
    // Project-level keys this build does not understand, preserved verbatim as raw JSON text so
    // that loading and re-saving a file written by a newer release does not silently drop data.
    // Keys are emitted again unchanged on save. Normally empty.
    std::map<std::string, std::string, std::less<>> extensions;
};

struct LoadedProject {
    timeline::Timeline timeline;
    ProjectMeta meta;
};

// Atomically writes `timeline` to `path`: a temp file in the same directory is flushed to disk,
// the previous version is kept as `path + ".bak"`, and the temp file is renamed over `path`. A
// failure at any step leaves the previous project intact and removes the temp file.
std::expected<void, ProjectError> saveProject(const std::filesystem::path& path,
                                              const timeline::Timeline& timeline,
                                              const ProjectMeta& meta = {});

// Reads a project written by saveProject. Audio sources stored relative to the project folder are
// resolved back to paths next to the project file; `generated:` sources are left unchanged.
std::expected<LoadedProject, ProjectError> loadProject(const std::filesystem::path& path);

// Test seam: when set, the hook runs after the temp file has been durably written but before any
// existing file is touched. Returning false simulates an I/O failure (for example a full disk) so
// tests can prove the previous project survives. Pass nullptr to clear it.
void setCommitHookForTesting(std::function<bool()> hook);
} // namespace wavy::project
