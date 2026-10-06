#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace wavy::effects {
struct Parameter {
    std::string id, name, unit;
    float minimum, maximum, defaultValue;
    float skew = 1;
};
class ParameterSet {
  public:
    explicit ParameterSet(std::vector<Parameter> definitions)
        : definitions_(std::move(definitions)),
          values_(std::make_unique<std::atomic<float>[]>(size())) {
        for (std::size_t i = 0; i < size(); ++i) {
            const auto& p = definitions_[i];
            if (p.id.empty() || std::isnan(p.minimum) || std::isnan(p.maximum) ||
                p.minimum > p.maximum || std::isnan(p.defaultValue) || p.defaultValue < p.minimum ||
                p.defaultValue > p.maximum || !std::isfinite(p.skew) || p.skew <= 0 ||
                index(p.id) != i)
                throw std::invalid_argument("Invalid parameter definition");
            set(i, p.defaultValue);
        }
    }
    std::size_t size() const noexcept { return definitions_.size(); }
    std::span<const Parameter> definitions() const noexcept { return definitions_; }
    std::size_t index(std::string_view id) const noexcept {
        for (std::size_t i = 0; i < size(); ++i)
            if (definitions_[i].id == id)
                return i;
        return size();
    }
    float get(std::size_t i) const noexcept {
        return i < size() ? values_[i].load(std::memory_order_relaxed) : 0;
    }
    float get(std::string_view id) const noexcept { return get(index(id)); }
    bool set(std::size_t i, float value) noexcept {
        if (i >= size())
            return false;
        const auto& p = definitions_[i];
        if (std::isnan(value))
            value = p.defaultValue;
        values_[i].store(std::clamp(value, p.minimum, p.maximum), std::memory_order_relaxed);
        return true;
    }
    bool set(std::string_view id, float value) noexcept { return set(index(id), value); }

  private:
    const std::vector<Parameter> definitions_;
    std::unique_ptr<std::atomic<float>[]> values_;
};
static_assert(std::atomic<float>::is_always_lock_free);
class Smoother {
  public:
    void prepare(double rate, double seconds = .005) noexcept {
        length_ = std::max<std::size_t>(1, static_cast<std::size_t>(rate * seconds));
    }
    void reset(double value) noexcept {
        value_ = target_ = value;
        remaining_ = 0;
    }
    void target(double value) noexcept {
        if (value != target_) {
            target_ = value;
            remaining_ = length_;
            step_ = (target_ - value_) / remaining_;
        }
    }
    bool settled() const noexcept { return remaining_ == 0; }
    double next() noexcept {
        if (remaining_ && !--remaining_)
            value_ = target_;
        else if (remaining_)
            value_ += step_;
        return value_;
    }

  private:
    double value_ = 0, target_ = 0, step_ = 0;
    std::size_t length_ = 1, remaining_ = 0;
};
} // namespace wavy::effects
