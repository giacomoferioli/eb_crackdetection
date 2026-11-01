#include "eblite/format.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <istream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

constexpr std::array<std::uint8_t, 8> magic{
    'E', 'B', 'L', 'I', 'T', 'E', '\r', '\n'};
constexpr std::size_t fixed_header_size = 32;
constexpr std::uint64_t maximum_header_size = 16U * 1024U * 1024U;

std::uint16_t read_u16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes.at(offset)) |
           static_cast<std::uint16_t>(bytes.at(offset + 1)) << 8U;
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4)
        throw std::runtime_error("truncated EBLITE header");
    return eblite::decode_u32_le(bytes.data() + offset);
}

std::uint64_t read_u64(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 8)
        throw std::runtime_error("truncated EBLITE header");
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= static_cast<std::uint64_t>(bytes[offset + i]) << (i * 8U);
    return value;
}

void write_u16(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint16_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value);
    bytes.at(offset + 1) = static_cast<std::uint8_t>(value >> 8U);
}

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint32_t value) {
    if (offset > bytes.size() || bytes.size() - offset < 4)
        throw std::runtime_error("EBLITE header buffer is too small");
    eblite::encode_u32_le(value, bytes.data() + offset);
}

void write_u64(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint64_t value) {
    if (offset > bytes.size() || bytes.size() - offset < 8)
        throw std::runtime_error("EBLITE header buffer is too small");
    for (unsigned i = 0; i < 8; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8U));
}

std::uint32_t crc32(const std::vector<std::uint8_t>& bytes) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::uint8_t byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U &
                                  (0U - static_cast<std::uint32_t>(crc & 1U)));
    }
    return ~crc;
}

std::size_t aligned_to_four(std::size_t value) {
    if (value > std::numeric_limits<std::size_t>::max() - 3U)
        throw std::runtime_error("invalid EBLITE metadata length");
    return (value + 3U) & ~std::size_t{3U};
}

void append_entry(std::vector<std::uint8_t>& bytes, std::size_t& cursor,
                  eblite::MetadataKey key, eblite::ValueType type,
                  std::uint64_t value, std::uint32_t value_size) {
    const std::size_t entry_end = cursor + 8U + value_size;
    if (entry_end > bytes.size())
        throw std::runtime_error("EBLITE metadata does not fit in the header");

    bytes[cursor] = static_cast<std::uint8_t>(key);
    bytes[cursor + 1] = static_cast<std::uint8_t>(type);
    write_u16(bytes, cursor + 2, 0);
    write_u32(bytes, cursor + 4, value_size);
    if (value_size == 4)
        write_u32(bytes, cursor + 8, static_cast<std::uint32_t>(value));
    else if (value_size == 8)
        write_u64(bytes, cursor + 8, value);
    else
        throw std::runtime_error("unsupported fixed EBLITE metadata size");
    cursor = aligned_to_four(entry_end);
}

} // namespace

namespace eblite {

std::uint32_t decode_u32_le(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           static_cast<std::uint32_t>(bytes[1]) << 8U |
           static_cast<std::uint32_t>(bytes[2]) << 16U |
           static_cast<std::uint32_t>(bytes[3]) << 24U;
}

void encode_u32_le(std::uint32_t value, std::uint8_t* bytes) noexcept {
    bytes[0] = static_cast<std::uint8_t>(value);
    bytes[1] = static_cast<std::uint8_t>(value >> 8U);
    bytes[2] = static_cast<std::uint8_t>(value >> 16U);
    bytes[3] = static_cast<std::uint8_t>(value >> 24U);
}

std::vector<std::uint8_t> encode_header(const Header& header) {
    if (header.event_stream_offset < fixed_header_size ||
        header.event_stream_offset > maximum_header_size ||
        header.event_stream_offset % 4U != 0)
        throw std::runtime_error("invalid EBLITE event-stream offset");
    if (header.sensor_width == 0 || header.sensor_width > maximum_x + 1U ||
        header.sensor_height == 0 || header.sensor_height > maximum_y + 1U)
        throw std::runtime_error("sensor dimensions do not fit EBLITE coordinates");
    if (header.recording_duration_us > maximum_timestamp_us)
        throw std::runtime_error("recording duration does not fit the 41-bit timestamp");
    if (header.event_count == 0 && header.recording_duration_us != 0)
        throw std::runtime_error("an empty EBLITE stream cannot have a duration");

    std::vector<std::uint8_t> bytes(
        static_cast<std::size_t>(header.event_stream_offset), 0);
    std::copy(magic.begin(), magic.end(), bytes.begin());
    write_u16(bytes, 8, format_major_version);
    write_u16(bytes, 10, format_minor_version);
    write_u32(bytes, 12, 0); // flags
    write_u64(bytes, 16, header.event_stream_offset);
    write_u32(bytes, 24, 4); // metadata entry count
    write_u32(bytes, 28, 0); // CRC field is zero while calculating the CRC

    std::size_t cursor = fixed_header_size;
    append_entry(bytes, cursor, MetadataKey::RecordingDurationUs,
                 ValueType::Unsigned64, header.recording_duration_us, 8);
    append_entry(bytes, cursor, MetadataKey::EventCount,
                 ValueType::Unsigned64, header.event_count, 8);
    append_entry(bytes, cursor, MetadataKey::SensorWidth,
                 ValueType::Unsigned32, header.sensor_width, 4);
    append_entry(bytes, cursor, MetadataKey::SensorHeight,
                 ValueType::Unsigned32, header.sensor_height, 4);

    write_u32(bytes, 28, crc32(bytes));
    return bytes;
}

Header decode_header(std::istream& input, std::uint64_t file_size) {
    std::array<std::uint8_t, fixed_header_size> prefix{};
    input.clear();
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(prefix.data()), prefix.size());
    if (input.gcount() != static_cast<std::streamsize>(prefix.size()))
        throw std::runtime_error("file is too small to contain an EBLITE header");
    if (!std::equal(magic.begin(), magic.end(), prefix.begin()))
        throw std::runtime_error("file does not have the EBLITE magic signature");

    const std::uint64_t event_stream_offset =
        static_cast<std::uint64_t>(decode_u32_le(prefix.data() + 16)) |
        static_cast<std::uint64_t>(decode_u32_le(prefix.data() + 20)) << 32U;
    if (event_stream_offset < fixed_header_size ||
        event_stream_offset > maximum_header_size ||
        event_stream_offset > file_size || event_stream_offset % 4U != 0)
        throw std::runtime_error("invalid EBLITE event-stream offset");

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(event_stream_offset));
    input.clear();
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (input.gcount() != static_cast<std::streamsize>(bytes.size()))
        throw std::runtime_error("truncated EBLITE header");

    const std::uint16_t major = read_u16(bytes, 8);
    if (major != format_major_version)
        throw std::runtime_error("unsupported EBLITE major version: " +
                                 std::to_string(major));
    const std::uint32_t flags = read_u32(bytes, 12);
    if (flags != 0)
        throw std::runtime_error("unsupported EBLITE header flags");

    const std::uint32_t expected_crc = read_u32(bytes, 28);
    write_u32(bytes, 28, 0);
    if (crc32(bytes) != expected_crc)
        throw std::runtime_error("EBLITE header CRC mismatch");
    write_u32(bytes, 28, expected_crc);

    Header header;
    header.event_stream_offset = event_stream_offset;
    bool have_duration = false;
    bool have_count = false;
    bool have_width = false;
    bool have_height = false;

    const std::uint32_t entry_count = read_u32(bytes, 24);
    std::size_t cursor = fixed_header_size;
    for (std::uint32_t entry = 0; entry < entry_count; ++entry) {
        if (cursor > bytes.size() || bytes.size() - cursor < 8)
            throw std::runtime_error("truncated EBLITE metadata entry");
        const auto key = static_cast<MetadataKey>(bytes[cursor]);
        const auto type = static_cast<ValueType>(bytes[cursor + 1]);
        const std::uint16_t entry_flags = read_u16(bytes, cursor + 2);
        const std::uint32_t length = read_u32(bytes, cursor + 4);
        const std::size_t value_offset = cursor + 8;
        if (entry_flags != 0)
            throw std::runtime_error("unsupported EBLITE metadata flags");
        if (value_offset > bytes.size() || bytes.size() - value_offset < length)
            throw std::runtime_error("truncated EBLITE metadata value");

        const auto require = [&](bool& present, ValueType expected_type,
                                 std::uint32_t expected_length) {
            if (present)
                throw std::runtime_error("duplicate required EBLITE metadata key");
            if (type != expected_type || length != expected_length)
                throw std::runtime_error("invalid type or size for EBLITE metadata key");
            present = true;
        };

        switch (key) {
        case MetadataKey::RecordingDurationUs:
            require(have_duration, ValueType::Unsigned64, 8);
            header.recording_duration_us = read_u64(bytes, value_offset);
            break;
        case MetadataKey::EventCount:
            require(have_count, ValueType::Unsigned64, 8);
            header.event_count = read_u64(bytes, value_offset);
            break;
        case MetadataKey::SensorWidth:
            require(have_width, ValueType::Unsigned32, 4);
            header.sensor_width = read_u32(bytes, value_offset);
            break;
        case MetadataKey::SensorHeight:
            require(have_height, ValueType::Unsigned32, 4);
            header.sensor_height = read_u32(bytes, value_offset);
            break;
        default:
            // Unknown keys remain safely skippable because every entry has a length.
            break;
        }

        const std::size_t unaligned_next = value_offset + length;
        const std::size_t next = aligned_to_four(unaligned_next);
        if (next > bytes.size())
            throw std::runtime_error("invalid EBLITE metadata padding");
        for (std::size_t i = unaligned_next; i < next; ++i)
            if (bytes[i] != 0)
                throw std::runtime_error("nonzero EBLITE metadata padding");
        cursor = next;
    }

    if (!have_duration || !have_count || !have_width || !have_height)
        throw std::runtime_error("EBLITE header is missing required metadata");
    for (std::size_t i = cursor; i < bytes.size(); ++i)
        if (bytes[i] != 0)
            throw std::runtime_error("nonzero bytes after EBLITE metadata");

    if (header.sensor_width == 0 || header.sensor_width > maximum_x + 1U ||
        header.sensor_height == 0 || header.sensor_height > maximum_y + 1U)
        throw std::runtime_error("EBLITE sensor dimensions exceed the event layout");
    if (header.recording_duration_us > maximum_timestamp_us)
        throw std::runtime_error("EBLITE duration exceeds the 41-bit timestamp");
    if (header.event_count == 0 && header.recording_duration_us != 0)
        throw std::runtime_error("empty EBLITE stream has a nonzero duration");
    const std::uint64_t available_event_bytes =
        file_size - header.event_stream_offset;
    if (header.event_count > available_event_bytes / 4U)
        throw std::runtime_error("EBLITE file is too small for its declared event count");
    return header;
}

} // namespace eblite
