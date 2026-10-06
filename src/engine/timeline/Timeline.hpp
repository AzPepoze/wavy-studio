#pragma once
#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace wavy::timeline {
using Frames = std::int64_t;
struct TrackId {
    std::uint32_t value = 0;
    auto operator<=>(const TrackId&) const = default;
};
struct ClipId {
    std::uint32_t value = 0;
    auto operator<=>(const ClipId&) const = default;
};
struct Clip {
    ClipId id;
    std::string source;
    Frames start = 0;
    Frames sourceOffset = 0;
    Frames length = 1;
    float gain = 1.f;
    Frames fadeIn = 0;
    Frames fadeOut = 0;
    bool operator==(const Clip&) const = default;
};
struct Track {
    TrackId id;
    std::string name;
    std::vector<Clip> clips;
    float gain = 1.f;
    bool muted = false;
    bool solo = false;
    bool operator==(const Track&) const = default;
};
namespace detail {
struct CommandAccess;
}
// Main-thread only. The audio thread must use a separate rendering snapshot, never Timeline.
// The lazy mutable range index makes even const queries unsafe for concurrent access.
class Timeline {
  public:
    unsigned sampleRate = 48000;
    const std::vector<Track>& tracks() const { return tracks_; }
    const Track* findTrack(TrackId id) const;
    struct ClipLocation {
        const Track* track;
        const Clip* clip;
    };
    std::optional<ClipLocation> findClip(ClipId id) const;
    Frames endFrame() const;
    std::vector<const Clip*> clipsInRange(TrackId track, Frames begin, Frames end) const;
    bool operator==(const Timeline& other) const {
        return sampleRate == other.sampleRate && tracks_ == other.tracks_;
    }
    // Next ids the command layer will hand out; zero means exhausted. Persisted so loading a
    // project can never reuse an id.
    TrackId nextTrackId() const { return nextTrack_; }
    ClipId nextClipId() const { return nextClip_; }
    // Builds a Timeline from persisted data (for example a loaded project file). Track and clip
    // ids are taken as given, clips are sorted by (start, id), and the id counters are preserved.
    // The caller is responsible for id uniqueness and validity; see project::loadProject.
    class Restorer {
      public:
        explicit Restorer(unsigned sampleRate = 48000) : sampleRate_(sampleRate) {}
        void addTrack(Track track);
        void setCounters(TrackId nextTrack, ClipId nextClip);
        [[nodiscard]] Timeline build();

      private:
        unsigned sampleRate_;
        std::vector<Track> tracks_;
        TrackId nextTrack_{1};
        ClipId nextClip_{1};
    };

  private:
    friend struct detail::CommandAccess;
    void invalidate(TrackId id);
    struct Index {
        TrackId id;
        std::vector<Frames> ends;
    };
    std::vector<Track> tracks_;
    mutable std::vector<Index> indexes_;
    TrackId nextTrack_{1};
    ClipId nextClip_{1};
};
} // namespace wavy::timeline
namespace std {
template <> struct hash<wavy::timeline::TrackId> {
    size_t operator()(wavy::timeline::TrackId id) const noexcept {
        return hash<uint32_t>{}(id.value);
    }
};
template <> struct hash<wavy::timeline::ClipId> {
    size_t operator()(wavy::timeline::ClipId id) const noexcept {
        return hash<uint32_t>{}(id.value);
    }
};
} // namespace std
