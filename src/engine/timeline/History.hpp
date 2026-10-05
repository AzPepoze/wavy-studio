#pragma once
#include "timeline/Commands.hpp"
#include <deque>

namespace wavy::timeline {
class History {
  public:
    explicit History(Timeline& timeline, size_t depth = 256) : timeline_(timeline), depth_(depth) {}
    Result execute(std::unique_ptr<Command> command);
    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    void clear();

  private:
    Timeline& timeline_;
    size_t depth_;
    std::deque<std::unique_ptr<Command>> undo_;
    std::deque<std::unique_ptr<Command>> redo_;
};
} // namespace wavy::timeline
