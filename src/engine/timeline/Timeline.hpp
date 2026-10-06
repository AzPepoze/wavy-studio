#pragma once
#include <cmath>
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
// Gain law applied over a fade region: Linear ramps amplitude directly; EqualPower ramps power, so
// overlapping fades keep constant power; Exponential is a fast, perceptually-tuned curve.
enum class FadeCurve { Linear, EqualPower, Exponential };
struct Clip {
    ClipId id;
    std::string source;
    Frames start = 0;
    Frames sourceOffset = 0;
    Frames length = 1;
    float gain = 1.f;
    Frames fadeIn = 0;
    Frames fadeOut = 0;
    bool muted = false;
    FadeCurve fadeInCurve = FadeCurve::EqualPower;
    FadeCurve fadeOutCurve = FadeCurve::EqualPower;
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
inline bool validFadeCurve(FadeCurve curve) {
    switch (curve) {
    case FadeCurve::Linear:
    case FadeCurve::EqualPower:
    case FadeCurve::Exponential:
        return true;
    }
    return false;
}
// Beat/frame conversions for a tempo and time signature. One beat is a quarter note, so the
// denominator only affects the bar length. Frames round to the nearest whole frame, halfway cases
// away from zero.
struct Tempo {
    double bpm = 120.0;
    unsigned numerator = 4;
    unsigned denominator = 4;
    unsigned sampleRate = 48000;
    double framesPerBeat() const { return sampleRate * 60.0 / bpm; }
    Frames beatsToFrames(double beats) const {
        return static_cast<Frames>(std::llround(beats * framesPerBeat()));
    }
    double framesToBeats(Frames frames) const {
        return static_cast<double>(frames) / framesPerBeat();
    }
    // A bar holds `numerator` notes of the denominator's length: numerator * 4 / denominator beats.
    double framesPerBar() const { return framesPerBeat() * numerator * 4.0 / denominator; }
};
namespace detail {
struct CommandAccess;
}
// Main-thread only. The audio thread must use a separate rendering snapshot, never Timeline.
// The lazy mutable range index makes even const queries unsafe for concurrent access.
class Timeline {
  public:
    unsigned sampleRate = 48000;
    double tempoBpm = 120.0;
    unsigned timeSignatureNumerator = 4;
    unsigned timeSignatureDenominator = 4;
    const std::vector<Track>& tracks() const { return tracks_; }
    const Track* findTrack(TrackId id) const;
    struct ClipLocation {
        const Track* track;
        const Clip* clip;
    };
    std::optional<ClipLocation> findClip(ClipId id) const;
    Frames endFrame() const;
    std::vector<const Clip*> clipsInRange(TrackId track, Frames begin, Frames end) const;
    Tempo tempo() const {
        return {tempoBpm, timeSignatureNumerator, timeSignatureDenominator, sampleRate};
    }
    double framesPerBeat() const { return tempo().framesPerBeat(); }
    Frames beatsToFrames(double beats) const { return tempo().beatsToFrames(beats); }
    double framesToBeats(Frames frames) const { return tempo().framesToBeats(frames); }
    double framesPerBar() const { return tempo().framesPerBar(); }
    bool operator==(const Timeline& other) const {
        return sampleRate == other.sampleRate && tempoBpm == other.tempoBpm &&
               timeSignatureNumerator == other.timeSignatureNumerator &&
               timeSignatureDenominator == other.timeSignatureDenominator &&
               tracks_ == other.tracks_;
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
        void setTempo(double bpm);
        void setTimeSignature(unsigned numerator, unsigned denominator);
        [[nodiscard]] Timeline build();

      private:
        unsigned sampleRate_;
        double tempoBpm_ = 120.0;
        unsigned timeSignatureNumerator_ = 4;
        unsigned timeSignatureDenominator_ = 4;
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
