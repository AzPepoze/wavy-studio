#include "timeline/History.hpp"
#include "core/Log.hpp"

namespace wavy::timeline {
Result History::execute(std::unique_ptr<Command> command) {
    if (!command)
        return Error::InvalidClip;
    const auto result = command->validate(timeline_);
    if (!result)
        return result;
    command->apply(timeline_);
    log::debug("timeline", "Execute {}", command->name());
    const bool merge = redo_.empty() && !undo_.empty() && undo_.back()->mergeWith(*command);
    redo_.clear();
    if (!merge && depth_ > 0) {
        undo_.push_back(std::move(command));
        if (undo_.size() > depth_)
            undo_.pop_front();
    }
    return result;
}
bool History::undo() {
    if (!canUndo())
        return false;
    auto command = std::move(undo_.back());
    undo_.pop_back();
    command->revert(timeline_);
    log::debug("timeline", "Undo {}", command->name());
    redo_.push_back(std::move(command));
    return true;
}
bool History::redo() {
    if (!canRedo())
        return false;
    auto command = std::move(redo_.back());
    redo_.pop_back();
    command->apply(timeline_);
    log::debug("timeline", "Redo {}", command->name());
    undo_.push_back(std::move(command));
    return true;
}
void History::clear() {
    undo_.clear();
    redo_.clear();
}
} // namespace wavy::timeline
