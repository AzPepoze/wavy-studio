#pragma once
#include "record/Recorder.hpp"
#include "timeline/History.hpp"

namespace wavy::record {
timeline::Result commitTake(timeline::History&, timeline::TrackId, const RecordedTake&);
} // namespace wavy::record
