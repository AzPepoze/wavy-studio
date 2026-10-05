#pragma once
#include "Timeline.hpp"
#include <memory>
#include <string_view>

namespace wavy::timeline {
enum class Error {
    None,
    UnknownTrack,
    UnknownClip,
    InvalidPosition,
    InvalidClip,
    InvalidGain,
    IdExhausted
};
class Result {
  public:
    Result(Error error = Error::None) : error_(error) {}
    explicit operator bool() const { return error_ == Error::None; }
    Error error() const { return error_; }

  private:
    Error error_;
};
class Command {
  public:
    virtual ~Command() = default;
    virtual Result validate(const Timeline&) const = 0;
    // Requires validate() to have succeeded against the current timeline.
    virtual void apply(Timeline&) = 0;
    virtual void revert(Timeline&) = 0;
    virtual std::string_view name() const = 0;
    virtual bool mergeWith(const Command&) { return false; }
};
enum class Edge { Left, Right };
class AddTrack final : public Command {
  public:
    explicit AddTrack(std::string name) : name_(std::move(name)) {}
    Result validate(const Timeline&) const override;
    void apply(Timeline&) override;
    void revert(Timeline&) override;
    std::string_view name() const override { return "AddTrack"; }
    TrackId trackId() const { return createdTrackId_; }

  private:
    std::string name_;
    TrackId createdTrackId_;
};
class RemoveTrack final : public Command {
  public:
    explicit RemoveTrack(TrackId id) : trackId_(id) {}
    Result validate(const Timeline&) const override;
    void apply(Timeline&) override;
    void revert(Timeline&) override;
    std::string_view name() const override { return "RemoveTrack"; }

  private:
    TrackId trackId_;
    std::optional<Track> removedTrack_;
    size_t previousIndex_ = 0;
};
class MoveTrack final : public Command {
  public:
    explicit MoveTrack(TrackId id, size_t index) : trackId_(id), destinationIndex_(index) {}
    Result validate(const Timeline&) const override;
    void apply(Timeline&) override;
    void revert(Timeline&) override;
    std::string_view name() const override { return "MoveTrack"; }

  private:
    TrackId trackId_;
    size_t destinationIndex_;
    std::optional<size_t> previousIndex_;
};
class AddClip final : public Command {
  public:
    explicit AddClip(TrackId id, Clip clip) : trackId_(id), clip_(std::move(clip)) {}
    Result validate(const Timeline&) const override;
    void apply(Timeline&) override;
    void revert(Timeline&) override;
    std::string_view name() const override { return "AddClip"; }
    ClipId createdClipId() const { return createdClipId_; }

  private:
    TrackId trackId_;
    Clip clip_;
    ClipId createdClipId_;
};
class RemoveClip final : public Command {
  public:
    explicit RemoveClip(ClipId id) : clipId_(id) {}
    Result validate(const Timeline&) const override;
    void apply(Timeline&) override;
    void revert(Timeline&) override;
    std::string_view name() const override { return "RemoveClip"; }

  private:
    ClipId clipId_;
    TrackId originalTrackId_;
    std::optional<Clip> originalClip_;
};
class MoveClip final : public Command {
  public:
    explicit MoveClip(ClipId id, TrackId track, Frames start)
        : clipId_(id), destinationTrackId_(track), newStart_(start) {}
    Result validate(const Timeline&) const override;
    void apply(Timeline&) override;
    void revert(Timeline&) override;
    std::string_view name() const override { return "MoveClip"; }

  private:
    ClipId clipId_;
    TrackId destinationTrackId_;
    Frames newStart_;
    TrackId originalTrackId_;
    std::optional<Frames> previousStart_;
};
class SplitClip final : public Command {
  public:
    explicit SplitClip(ClipId id, Frames at) : clipId_(id), splitFrame_(at) {}
    Result validate(const Timeline&) const override;
    void apply(Timeline&) override;
    void revert(Timeline&) override;
    std::string_view name() const override { return "SplitClip"; }
    ClipId createdClipId() const { return createdClipId_; }

  private:
    ClipId clipId_;
    Frames splitFrame_;
    TrackId originalTrackId_;
    std::optional<Clip> originalClip_;
    ClipId createdClipId_;
};
class TrimClip final : public Command {
  public:
    explicit TrimClip(ClipId id, Edge edge, Frames at) : clipId_(id), edge_(edge), edgeFrame_(at) {}
    Result validate(const Timeline&) const override;
    void apply(Timeline&) override;
    void revert(Timeline&) override;
    std::string_view name() const override { return "TrimClip"; }

  private:
    ClipId clipId_;
    Edge edge_;
    Frames edgeFrame_;
    TrackId originalTrackId_;
    std::optional<Clip> originalClip_;
};
class DuplicateClip final : public Command {
  public:
    explicit DuplicateClip(ClipId id, std::optional<Frames> start = std::nullopt)
        : clipId_(id), newStart_(start) {}
    Result validate(const Timeline&) const override;
    void apply(Timeline&) override;
    void revert(Timeline&) override;
    std::string_view name() const override { return "DuplicateClip"; }
    ClipId createdClipId() const { return createdClipId_; }

  private:
    ClipId clipId_;
    std::optional<Frames> newStart_;
    ClipId createdClipId_;
    TrackId originalTrackId_;
};
class SetClipGain final : public Command {
  public:
    explicit SetClipGain(ClipId id, float gain) : clipId_(id), newGain_(gain) {}
    Result validate(const Timeline&) const override;
    void apply(Timeline&) override;
    void revert(Timeline&) override;
    std::string_view name() const override { return "SetClipGain"; }
    bool mergeWith(const Command&) override;

  private:
    ClipId clipId_;
    float newGain_;
    std::optional<float> previousGain_;
};
class SetTrackGain final : public Command {
  public:
    explicit SetTrackGain(TrackId id, float gain) : trackId_(id), newGain_(gain) {}
    Result validate(const Timeline&) const override;
    void apply(Timeline&) override;
    void revert(Timeline&) override;
    std::string_view name() const override { return "SetTrackGain"; }
    bool mergeWith(const Command&) override;

  private:
    TrackId trackId_;
    float newGain_;
    std::optional<float> previousGain_;
};
class SetTrackMute final : public Command {
  public:
    explicit SetTrackMute(TrackId id, bool muted) : trackId_(id), newMuted_(muted) {}
    Result validate(const Timeline&) const override;
    void apply(Timeline&) override;
    void revert(Timeline&) override;
    std::string_view name() const override { return "SetTrackMute"; }
    bool mergeWith(const Command&) override;

  private:
    TrackId trackId_;
    bool newMuted_;
    std::optional<bool> previousMuted_;
};
class SetTrackSolo final : public Command {
  public:
    explicit SetTrackSolo(TrackId id, bool solo) : trackId_(id), newSolo_(solo) {}
    Result validate(const Timeline&) const override;
    void apply(Timeline&) override;
    void revert(Timeline&) override;
    std::string_view name() const override { return "SetTrackSolo"; }
    bool mergeWith(const Command&) override;

  private:
    TrackId trackId_;
    bool newSolo_;
    std::optional<bool> previousSolo_;
};
} // namespace wavy::timeline
