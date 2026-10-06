#include "TimelineModel.hpp"
#include "timeline/Commands.hpp"
#include <algorithm>
#include <array>
#include <limits>

using namespace wavy::timeline;

namespace {
class EditClip final : public Command {
  public:
    EditClip(ClipId id, TrackId target, Frames start, Frames length)
        : id_(id), target_(target), start_(start), length_(length) {}
    Result validate(const Timeline& timeline) const override {
        const auto clip = timeline.findClip(id_);
        return clip && timeline.findTrack(target_) && start_ >= 0 && length_ > 0 &&
                       length_ <= std::numeric_limits<Frames>::max() - start_
                   ? Result{}
                   : Result{Error::InvalidClip};
    }
    void apply(Timeline& timeline) override {
        const auto original = timeline.findClip(id_);
        const auto oldStart = original->clip->start;
        const auto oldEnd = oldStart + original->clip->length;
        const bool leftTrim = start_ > oldStart && start_ + length_ == oldEnd;
        if (leftTrim) {
            TrimClip trim(id_, Edge::Left, start_);
            trim.apply(timeline);
            commands_.push_back(std::make_unique<TrimClip>(std::move(trim)));
        }
        if (original->track->id != target_ || (!leftTrim && oldStart != start_)) {
            MoveClip move(id_, target_, start_);
            move.apply(timeline);
            commands_.push_back(std::make_unique<MoveClip>(std::move(move)));
        }
        auto clip = timeline.findClip(id_);
        const Frames end = start_ + length_;
        if (clip->clip->start + clip->clip->length != end) {
            TrimClip trim(id_, Edge::Right, end);
            trim.apply(timeline);
            commands_.push_back(std::make_unique<TrimClip>(std::move(trim)));
        }
    }
    void revert(Timeline& timeline) override {
        for (auto it = commands_.rbegin(); it != commands_.rend(); ++it)
            (*it)->revert(timeline);
        commands_.clear();
    }
    std::string_view name() const override { return "EditClip"; }

  private:
    ClipId id_;
    TrackId target_;
    Frames start_;
    Frames length_;
    std::vector<std::unique_ptr<Command>> commands_;
};
} // namespace

TimelineModel::TimelineModel(QObject* parent) : QAbstractListModel(parent) {
    constexpr std::array names{"Drums", "Bass", "Keys", "Vocals"};
    std::array<TrackId, names.size()> tracks;
    for (size_t i = 0; i < names.size(); ++i) {
        history_.execute(std::make_unique<AddTrack>(names[i]));
        tracks[i] = TrackId{static_cast<uint32_t>(i + 1)};
    }
    for (size_t t = 0; t < tracks.size(); ++t) {
        const int count = t < 2 ? 3 : 2;
        for (int c = 0; c < count; ++c) {
            const Frames start = static_cast<Frames>((c * 3 + t * 0.4) * sampleRate());
            history_.execute(std::make_unique<AddClip>(
                tracks[t], Clip{{},
                                "Take " + std::to_string(c + 1),
                                start,
                                0,
                                static_cast<Frames>(sampleRate() * 2.4)}));
        }
    }
    history_.clear();
}

int TimelineModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(timeline_.tracks().size());
}

QVariant TimelineModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const auto& track = timeline_.tracks()[static_cast<size_t>(index.row())];
    switch (role) {
    case TrackIdRole:
        return track.id.value;
    case NameRole:
        return QString::fromStdString(track.name);
    case MutedRole:
        return track.muted;
    case SoloRole:
        return track.solo;
    default:
        return {};
    }
}

QHash<int, QByteArray> TimelineModel::roleNames() const {
    return {{TrackIdRole, "trackId"}, {NameRole, "name"}, {MutedRole, "muted"}, {SoloRole, "solo"}};
}

qint64 TimelineModel::durationFrames() const {
    return std::max<qint64>(static_cast<qint64>(sampleRate()) * 30,
                            timeline_.endFrame() + sampleRate() * 4LL);
}

void TimelineModel::setPlayheadFrame(qint64 frame) {
    frame = std::max<qint64>(0, frame);
    if (frame == playheadFrame_)
        return;
    playheadFrame_ = frame;
    emit playheadFrameChanged();
}

QVariantList TimelineModel::visibleClips(int trackId, qint64 firstFrame, qint64 lastFrame) const {
    QVariantList result;
    const auto clips =
        timeline_.clipsInRange(TrackId{static_cast<uint32_t>(trackId)}, firstFrame, lastFrame);
    for (const auto* clip : clips) {
        const auto hue = static_cast<int>((clip->id.value * 47U) % 360U);
        result.push_back(QVariantMap{{"clipId", clip->id.value},
                                     {"name", QString::fromStdString(clip->source)},
                                     {"startFrame", QVariant::fromValue<qint64>(clip->start)},
                                     {"durationFrames", QVariant::fromValue<qint64>(clip->length)},
                                     {"color", QString("hsl(%1, 38%, 45%)").arg(hue)}});
    }
    return result;
}

bool TimelineModel::execute(std::unique_ptr<Command> command) {
    if (!history_.execute(std::move(command)))
        return false;
    emit historyChanged();
    emit durationFramesChanged();
    return true;
}

void TimelineModel::notifyTimeline() {
    emit durationFramesChanged();
    for (const auto& track : timeline_.tracks())
        emit clipsChangedForTrack(static_cast<int>(track.id.value));
}

void TimelineModel::editClip(int clipId, int targetTrackId, qint64 start, qint64 duration) {
    const ClipId id{static_cast<uint32_t>(clipId)};
    const auto current = timeline_.findClip(id);
    if (!current || duration <= 0 || start < 0)
        return;
    const auto oldTrack = current->track->id;
    const TrackId target{static_cast<uint32_t>(targetTrackId)};
    if (!execute(std::make_unique<EditClip>(id, target, start, duration)))
        return;
    notifyTimeline();
    if (oldTrack != target)
        emit clipsChangedForTrack(static_cast<int>(oldTrack.value));
}

void TimelineModel::action(int clipId, int trackId, const QString& operation) {
    const ClipId id{static_cast<uint32_t>(clipId)};
    const auto location = timeline_.findClip(id);
    if (!location || location->track->id.value != static_cast<uint32_t>(trackId))
        return;
    const int affected = static_cast<int>(location->track->id.value);
    bool changed = false;
    if (operation == "delete")
        changed = execute(std::make_unique<RemoveClip>(id));
    else if (operation == "duplicate")
        changed = execute(std::make_unique<DuplicateClip>(id));
    else if (operation == "split")
        changed = execute(std::make_unique<SplitClip>(id, playheadFrame_));
    if (changed) {
        emit clipsChangedForTrack(affected);
        emit durationFramesChanged();
    }
}

void TimelineModel::setTrackState(int row, const QString& role, bool value) {
    if (row < 0 || row >= rowCount())
        return;
    const auto id = timeline_.tracks()[static_cast<size_t>(row)].id;
    bool changed = false;
    if (role == "muted")
        changed = execute(std::make_unique<SetTrackMute>(id, value));
    else if (role == "solo")
        changed = execute(std::make_unique<SetTrackSolo>(id, value));
    if (changed)
        emit dataChanged(index(row), index(row), {role == "muted" ? MutedRole : SoloRole});
}

int TimelineModel::trackIdAt(int row) const {
    return row >= 0 && row < rowCount()
               ? static_cast<int>(timeline_.tracks()[static_cast<size_t>(row)].id.value)
               : -1;
}

void TimelineModel::addTrack(const QString& name) {
    const int row = rowCount();
    auto command = std::make_unique<AddTrack>(name.toStdString());
    if (!command->validate(timeline_))
        return;
    beginInsertRows({}, row, row);
    execute(std::move(command));
    endInsertRows();
}

void TimelineModel::notifyAllTracks() {
    if (rowCount())
        emit dataChanged(index(0), index(rowCount() - 1), {MutedRole, SoloRole});
    notifyTimeline();
    emit historyChanged();
}

void TimelineModel::undo() {
    if (history_.canUndo()) {
        beginResetModel();
        const bool changed = history_.undo();
        endResetModel();
        if (!changed)
            return;
        notifyAllTracks();
    }
}
void TimelineModel::redo() {
    if (history_.canRedo()) {
        beginResetModel();
        const bool changed = history_.redo();
        endResetModel();
        if (!changed)
            return;
        notifyAllTracks();
    }
}
