#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <vector>

namespace eblite {

constexpr std::uint16_t format_major_version = 1;
constexpr std::uint16_t format_minor_version = 0;
constexpr std::uint64_t default_event_stream_offset = 128;
constexpr std::uint64_t maximum_timestamp_us = (std::uint64_t{1} << 41U) - 1U;
constexpr std::uint16_t maximum_x = (std::uint16_t{1} << 11U) - 1U;
constexpr std::uint16_t maximum_y = (std::uint16_t{1} << 10U) - 1U;

enum class MetadataKey : std::uint8_t {
    RecordingDurationUs = 1,
    EventCount = 2,
    SensorWidth = 3,
    SensorHeight = 4,
};

enum class ValueType : std::uint8_t {
    Unsigned32 = 1,
    Signed32 = 2,
    Float32 = 3,
    Unsigned64 = 4,
    Utf8 = 5,
    Bytes = 6,
};

struct Header {
    std::uint64_t event_stream_offset = default_event_stream_offset;
    std::uint64_t recording_duration_us = 0;
    std::uint64_t event_count = 0;
    std::uint32_t sensor_width = 0;
    std::uint32_t sensor_height = 0;
};

std::vector<std::uint8_t> encode_header(const Header& header);
Header decode_header(std::istream& input, std::uint64_t file_size);

std::uint32_t decode_u32_le(const std::uint8_t* bytes) noexcept;
void encode_u32_le(std::uint32_t value, std::uint8_t* bytes) noexcept;

} // namespace eblite
