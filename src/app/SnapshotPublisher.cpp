#include "SnapshotPublisher.hpp"
#include "TimelineModel.hpp"
#include "core/Log.hpp"
#include <QMetaObject>

SnapshotPublisher::SnapshotPublisher(TimelineModel& model, wavy::Mixer& mixer, QObject* parent)
    : QObject(parent), model_(model), mixer_(mixer),
      library_(std::make_unique<wavy::SourceLibrary>(model.timeline().sampleRate)) {
    library_->setOnReady([this](const std::string&, bool) {
        QMetaObject::invokeMethod(this, [this] { dirty_ = true; }, Qt::QueuedConnection);
    });
    connect(&model_, &TimelineModel::timelineChanged, this, [this] { dirty_ = true; });
    timer_.setInterval(20);
    connect(&timer_, &QTimer::timeout, this, &SnapshotPublisher::update);
    timer_.start();
}

SnapshotPublisher::~SnapshotPublisher() {
    // Join callbacks before QObject teardown; queued calls are removed by QObject's destructor.
    library_.reset();
    mixer_.collectRetired();
}

void SnapshotPublisher::update() {
    mixer_.collectRetired();
    if (!dirty_)
        return;
    dirty_ = false;
    wavy::SourceCache cache;
    for (const auto& track : model_.timeline().tracks())
        for (const auto& clip : track.clips) {
            if (cache.contains(clip.source))
                continue;
            library_->request(clip.source);
            const auto source = library_->get(clip.source);
            // A source that failed to load is reported once; one that is still loading is expected.
            if (!source && library_->state(clip.source) == wavy::SourceLibrary::State::Failed &&
                reportedFailures_.insert(clip.source).second)
                wavy::log::error("source", "Cannot load {}: {}", clip.source,
                                 wavy::toString(library_->error(clip.source)));
            // Null entries prevent buildSnapshot from decoding on the control thread.
            cache.emplace(clip.source, source ? source->audio : nullptr);
        }
    mixer_.publish(wavy::buildSnapshot(model_.timeline(), cache));
    emit published();
}
