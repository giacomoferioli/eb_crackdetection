#pragma once

#include <cstdint>

namespace timing {

// Monotonic time since an arbitrary fixed point. Suitable for measuring
// intervals, but intentionally unrelated to wall-clock/calendar time.
std::uint64_t ms();
std::uint64_t us();

class Timer {
public:
    Timer();

    void ref();
    std::uint64_t ms_since_ref() const;
    std::uint64_t us_since_ref() const;

private:
    std::uint64_t reference_us_;
};

} // namespace timing
