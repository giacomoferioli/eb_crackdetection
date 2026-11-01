#pragma once

#include "videosource/event_source.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace eblite {

// Streaming EBLITE encoder. Event data is written immediately; only a bounded
// byte buffer and the final header counters are retained in memory.
class Writer {
public:
    Writer(const std::filesystem::path& path, std::uint32_t sensor_width,
           std::uint32_t sensor_height);
    ~Writer();

    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    void write(const Event* begin, const Event* end);
    void write(const std::vector<Event>& events) {
        write(events.data(), events.data() + events.size());
    }
    void finalize();

    std::uint64_t event_count() const noexcept { return event_count_; }
    std::uint64_t recording_duration_us() const noexcept {
        return recording_duration_us_;
    }
    bool finalized() const noexcept { return finalized_; }

private:
    void append_u32(std::uint32_t value);
    void flush_buffer();

    std::filesystem::path path_;
    std::ofstream output_;
    std::vector<std::uint8_t> buffer_;
    std::size_t buffered_bytes_ = 0;
    std::uint32_t sensor_width_;
    std::uint32_t sensor_height_;
    std::uint64_t event_count_ = 0;
    std::uint64_t recording_duration_us_ = 0;
    std::int64_t first_input_timestamp_us_ = 0;
    std::int64_t previous_input_timestamp_us_ = 0;
    bool finalized_ = false;
};

} // namespace eblite
