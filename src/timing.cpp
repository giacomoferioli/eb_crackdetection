#include "timing.hpp"

#include <chrono>

namespace {
using Clock = std::chrono::steady_clock;
const Clock::time_point origin = Clock::now();
}

namespace timing {

std::uint64_t us() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - origin)
            .count());
}

std::uint64_t ms() { return us() / 1000U; }

Timer::Timer() : reference_us_(us()) {}

void Timer::ref() { reference_us_ = us(); }

std::uint64_t Timer::ms_since_ref() const {
    return us_since_ref() / 1000U;
}

std::uint64_t Timer::us_since_ref() const { return us() - reference_us_; }

} // namespace timing
