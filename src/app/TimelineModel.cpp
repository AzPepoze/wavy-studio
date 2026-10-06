#include "TimelineModel.hpp"
#include "record/CommitTake.hpp"
#include "timeline/Commands.hpp"
#include <QFileInfo>
#include <QStringList>
#include <algorithm>
#include <array>
#include <cmath>

using namespace wavy::timeline;

namespace {
constexpr double kMinimumGainDb = -60.0;
constexpr double kMaximumGainDb = 6.0;

double linearToDecibels(float gain) {
    return gain > 0.f ? 20.0 * std::log10(gain) : kMinimumGainDb;
}

float decibelsToLinear(double db) { return static_cast<float>(std::pow(10.0, db / 20.0)); }

QString friendlyClipName(const std::string& source) {
    const QString text = QString::fromStdString(source);
    if (!text.startsWith(QStringLiteral("generated:")))
        return QFileInfo(text).baseName();
    const QStringList parts = text.split(QLatin1Char(':'));
    const QString kind = parts.value(1);
    if (kind == QStringLiteral("noise"))
        return QStringLiteral("Noise");
    const QString label =
        kind.isEmpty() ? QStringLiteral("Tone") : kind.left(1).toUpper() + kind.mid(1);
    return parts.size() > 2 ? label + QStringLiteral(" ") + parts.at(2) + QStringLiteral(" Hz")
                            : label;
}
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
                                "generated:sine:" + std::to_string(110 * (t + 1)) + ":0.2:12",
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
    case GainRole:
        return linearToDecibels(track.gain);
    default:
        return {};
    }
}

QHash<int, QByteArray> TimelineModel::roleNames() const {
    return {{TrackIdRole, "trackId"},
            {NameRole, "name"},
            {MutedRole, "muted"},
            {SoloRole, "solo"},
            {GainRole, "gain"}};
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
    const TrackId id{static_cast<uint32_t>(trackId)};
    // Track ids are allocated in creation order and never reused, so this palette index stays
    // with the track when tracks reorder.
    const int trackIndex = id.value > 0 ? static_cast<int>(id.value - 1) : 0;
    const auto clips = timeline_.clipsInRange(id, firstFrame, lastFrame);
    for (const auto* clip : clips)
        result.push_back(QVariantMap{{"clipId", clip->id.value},
                                     {"name", friendlyClipName(clip->source)},
                                     {"startFrame", QVariant::fromValue<qint64>(clip->start)},
                                     {"durationFrames", QVariant::fromValue<qint64>(clip->length)},
                                     {"trackIndex", trackIndex}});
    return result;
}

bool TimelineModel::execute(std::unique_ptr<Command> command) {
    if (!history_.execute(std::move(command)))
        return false;
    emit historyChanged();
    emit timelineChanged();
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

void TimelineModel::setTrackGain(int trackId, double db) {
    const TrackId id{static_cast<uint32_t>(trackId)};
    if (!timeline_.findTrack(id))
        return;
    db = std::clamp(db, kMinimumGainDb, kMaximumGainDb);
    if (!execute(std::make_unique<SetTrackGain>(id, decibelsToLinear(db))))
        return;
    for (int row = 0; row < rowCount(); ++row)
        if (timeline_.tracks()[static_cast<size_t>(row)].id == id) {
            emit dataChanged(index(row), index(row), {GainRole});
            break;
        }
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
    emit timelineChanged();
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

void TimelineModel::addClip(int trackId, const QString& source, qint64 start, qint64 length) {
    if (execute(std::make_unique<AddClip>(TrackId{static_cast<uint32_t>(trackId)},
                                          Clip{{}, source.toStdString(), start, 0, length})))
        emit clipsChangedForTrack(trackId);
}

bool TimelineModel::commitTake(int trackId, const wavy::record::RecordedTake& take) {
    if (!wavy::record::commitTake(history_, TrackId{static_cast<uint32_t>(trackId)}, take))
        return false;
    emit historyChanged();
    emit timelineChanged();
    emit durationFramesChanged();
    emit clipsChangedForTrack(trackId);
    return true;
}
