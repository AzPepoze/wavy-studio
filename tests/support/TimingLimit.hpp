#pragma once
#include <cstdlib>

// Wall-clock limits are tight on a developer machine, but CI runners are slow, shared and run
// several test suites at once. Unless WAVY_STRICT_TIMING is set, a limit only has to catch an
// order-of-magnitude regression; set it locally to enforce the real limit.
inline double timingLimit(double limit) {
    return std::getenv("WAVY_STRICT_TIMING") ? limit : limit * 6;
}
