#pragma once
#include "timeline/Commands.hpp"

namespace wavy::timeline {
class CompoundCommand final : public Command {
  public:
    CompoundCommand(std::string name, std::vector<std::unique_ptr<Command>> commands)
        : name_(std::move(name)), commands_(std::move(commands)) {}
    std::unique_ptr<Command> clone() const override;
    Result validate(const Timeline&) const override;
    void apply(Timeline&) override;
    void revert(Timeline&) override;
    std::string_view name() const override { return name_; }
    bool mergeWith(const Command&) override { return false; }

  private:
    std::string name_;
    std::vector<std::unique_ptr<Command>> commands_;
};
} // namespace wavy::timeline
