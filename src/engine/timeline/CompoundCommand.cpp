#include "timeline/CompoundCommand.hpp"
#include <cassert>

namespace wavy::timeline {
std::unique_ptr<Command> CompoundCommand::clone() const {
    std::vector<std::unique_ptr<Command>> commands;
    for (const auto& command : commands_)
        commands.push_back(command ? command->clone() : nullptr);
    return std::make_unique<CompoundCommand>(name_, std::move(commands));
}
Result CompoundCommand::validate(const Timeline& timeline) const {
    // Clone commands too: scratch application must not consume ids or capture undo state.
    auto scratch = timeline;
    for (const auto& command : commands_) {
        if (!command)
            return Error::InvalidClip;
        auto step = command->clone();
        const auto result = step->validate(scratch);
        if (!result)
            return result;
        step->apply(scratch);
    }
    return {};
}
void CompoundCommand::apply(Timeline& timeline) {
    assert(validate(timeline));
    for (const auto& command : commands_)
        command->apply(timeline);
}
void CompoundCommand::revert(Timeline& timeline) {
    for (auto it = commands_.rbegin(); it != commands_.rend(); ++it)
        (*it)->revert(timeline);
}
} // namespace wavy::timeline
