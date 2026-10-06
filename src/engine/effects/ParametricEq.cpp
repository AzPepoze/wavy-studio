#include "effects/ParametricEq.hpp"
#include <numbers>
#include <stdexcept>
namespace wavy::effects {
namespace {
std::vector<Parameter> definitions() {
    std::vector<Parameter> result;
    constexpr float frequencies[]{120, 500, 2500, 8000};
    for (int i = 0; i < 4; ++i) {
        const auto id = "band" + std::to_string(i + 1) + ".";
        result.push_back({id + "type", "Type", "", 0, 2, i == 0 ? 0.f : (i == 3 ? 2.f : 1.f)});
        result.push_back({id + "frequency", "Frequency", "Hz", 20, 40000, frequencies[i], .3f});
        result.push_back({id + "gain", "Gain", "dB", -24, 24, 0});
        result.push_back({id + "q", "Q", "", .1f, 18, .70710678f, .3f});
        result.push_back({id + "enabled", "Enabled", "", 0, 1, 1});
    }
    return result;
}
} // namespace
ParametricEq::ParametricEq(std::shared_ptr<ParameterSet> params)
    : params_(sharedParameters(std::move(params), definitions())) {}
ParametricEq::Coefficients ParametricEq::coefficients(std::size_t band) const noexcept {
    const auto i = band * 5;
    const double gain = params_->get(i + 2);
    if (params_->get(i + 4) < .5f || gain == 0)
        return {1, 0, 0, 0, 0};
    const double w =
        2 * std::numbers::pi * std::clamp<double>(params_->get(i + 1), 1, rate_ * .49) / rate_;
    const double c = std::cos(w), s = std::sin(w);
    const double a = std::pow(10., gain / 40), alpha = s / (2 * params_->get(i + 3));
    double b0, b1, b2, a0, a1, a2;
    const auto type = static_cast<Type>(std::lround(params_->get(i)));
    if (type == Type::Peak) {
        b0 = 1 + alpha * a;
        b1 = -2 * c;
        b2 = 1 - alpha * a;
        a0 = 1 + alpha / a;
        a1 = -2 * c;
        a2 = 1 - alpha / a;
    } else {
        const double beta = 2 * std::sqrt(a) * alpha;
        if (type == Type::LowShelf) {
            b0 = a * ((a + 1) - (a - 1) * c + beta);
            b1 = 2 * a * ((a - 1) - (a + 1) * c);
            b2 = a * ((a + 1) - (a - 1) * c - beta);
            a0 = (a + 1) + (a - 1) * c + beta;
            a1 = -2 * ((a - 1) + (a + 1) * c);
            a2 = (a + 1) + (a - 1) * c - beta;
        } else {
            b0 = a * ((a + 1) + (a - 1) * c + beta);
            b1 = -2 * a * ((a - 1) + (a + 1) * c);
            b2 = a * ((a + 1) + (a - 1) * c - beta);
            a0 = (a + 1) - (a - 1) * c + beta;
            a1 = 2 * ((a - 1) - (a + 1) * c);
            a2 = (a + 1) - (a - 1) * c - beta;
        }
    }
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}
void ParametricEq::prepare(double rate, std::size_t) {
    if (!std::isfinite(rate) || rate < 100)
        throw std::invalid_argument("Invalid effect sample rate");
    rate_ = rate;
    for (auto& band : bands_)
        for (auto& coefficient : band.coefficients)
            coefficient.prepare(rate);
    reset();
}
void ParametricEq::reset() noexcept {
    for (std::size_t b = 0; b < bands_.size(); ++b) {
        bands_[b].state = {};
        const auto c = coefficients(b);
        for (std::size_t i = 0; i < c.size(); ++i)
            bands_[b].coefficients[i].reset(c[i]);
    }
}
void ParametricEq::process(float* stereo, std::size_t frames) noexcept {
    for (std::size_t b = 0; b < bands_.size(); ++b) {
        auto& band = bands_[b];
        const auto target = coefficients(b);
        // Stable second-order denominators form a convex region, so linear interpolation stays
        // stable.
        for (std::size_t i = 0; i < target.size(); ++i)
            band.coefficients[i].target(target[i]);
        const bool settled = std::all_of(band.coefficients.begin(), band.coefficients.end(),
                                         [](const auto& c) { return c.settled(); });
        if (target == Coefficients{1, 0, 0, 0, 0} && settled &&
            band.state == std::array<std::array<double, 2>, 2>{})
            continue;
        auto process = [&](const Coefficients& c, std::size_t frame) {
            for (unsigned channel = 0; channel < 2; ++channel) {
                auto& state = band.state[channel];
                const double x = stereo[frame * 2 + channel];
                const double y = c[0] * x + state[0];
                state[0] = c[1] * x - c[3] * y + state[1];
                state[1] = c[2] * x - c[4] * y;
                stereo[frame * 2 + channel] = static_cast<float>(y);
            }
        };
        if (settled) {
            const auto [b0, b1, b2, a1, a2] = target;
            auto [l1, l2] = band.state[0];
            auto [r1, r2] = band.state[1];
            for (std::size_t frame = 0; frame < frames; ++frame) {
                const double l = stereo[frame * 2], r = stereo[frame * 2 + 1];
                const double left = b0 * l + l1, right = b0 * r + r1;
                l1 = b1 * l - a1 * left + l2;
                l2 = b2 * l - a2 * left;
                r1 = b1 * r - a1 * right + r2;
                r2 = b2 * r - a2 * right;
                stereo[frame * 2] = static_cast<float>(left);
                stereo[frame * 2 + 1] = static_cast<float>(right);
            }
            band.state = {{{l1, l2}, {r1, r2}}};
        } else {
            for (std::size_t frame = 0; frame < frames; ++frame) {
                Coefficients c;
                for (std::size_t i = 0; i < c.size(); ++i)
                    c[i] = band.coefficients[i].next();
                process(c, frame);
            }
        }
        // 1e-30 is inaudible and far above double subnormals; slow state decay permits a block
        // check.
        for (auto& state : band.state)
            for (auto& z : state)
                if (std::abs(z) < 1e-30)
                    z = 0;
    }
    bool finite = true;
    for (std::size_t i = 0; i < frames * 2; ++i)
        finite &= std::isfinite(stereo[i]);
    if (!finite) {
        for (auto& band : bands_)
            band.state = {};
        std::fill_n(stereo, frames * 2, 0.f);
    }
}
} // namespace wavy::effects
