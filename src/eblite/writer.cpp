#include "eblite/writer.hpp"

#include "eblite/format.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

constexpr std::size_t output_buffer_size = 4U * 1024U * 1024U;
constexpr std::uint32_t full_event_bit = std::uint32_t{1} << 31U;
constexpr std::uint32_t polarity_bit = std::uint32_t{1} << 30U;
constexpr unsigned x_shift = 19;
constexpr unsigned y_shift = 9;
constexpr std::uint32_t time_payload_mask = 0x1FFU;

} // namespace

namespace eblite {

Writer::Writer(const std::filesystem::path& path, std::uint32_t sensor_width,
               std::uint32_t sensor_height)
    : path_(path), buffer_(output_buffer_size), sensor_width_(sensor_width),
      sensor_height_(sensor_height) {
    Header header;
    header.sensor_width = sensor_width_;
    header.sensor_height = sensor_height_;
    const std::vector<std::uint8_t> encoded_header = encode_header(header);

    output_.open(path_, std::ios::binary | std::ios::trunc);
    if (!output_)
        throw std::runtime_error("could not create EBLITE file: " + path_.string());
    output_.write(reinterpret_cast<const char*>(encoded_header.data()),
                  static_cast<std::streamsize>(encoded_header.size()));
    if (!output_)
        throw std::runtime_error("could not write EBLITE header: " + path_.string());
}

Writer::~Writer() {
    try {
        finalize();
    } catch (...) {
        // Destructors cannot report I/O errors. Explicit finalize() does.
    }
}

void Writer::append_u32(std::uint32_t value) {
    if (buffer_.size() - buffered_bytes_ < 4)
        flush_buffer();
    encode_u32_le(value, buffer_.data() + buffered_bytes_);
    buffered_bytes_ += 4;
}

void Writer::flush_buffer() {
    if (buffered_bytes_ == 0)
        return;
    output_.write(reinterpret_cast<const char*>(buffer_.data()),
                  static_cast<std::streamsize>(buffered_bytes_));
    if (!output_)
        throw std::runtime_error("failed writing EBLITE event stream: " +
                                 path_.string());
    buffered_bytes_ = 0;
}

void Writer::write(const Event* begin, const Event* end) {
    if (finalized_)
        throw std::runtime_error("cannot write to a finalized EBLITE file");
    if (!begin || begin == end)
        return;
    if (end < begin)
        throw std::runtime_error("invalid EBLITE event range");

    for (const Event* event = begin; event != end; ++event) {
        if (event->x >= sensor_width_ || event->x > maximum_x ||
            event->y >= sensor_height_ || event->y > maximum_y)
            throw std::runtime_error("event coordinates exceed the EBLITE sensor geometry");
        if (event_count_ != 0 &&
            event->timestamp_us < previous_input_timestamp_us_)
            throw std::runtime_error("EBLITE input events are not timestamp ordered");

        if (event_count_ == 0) {
            first_input_timestamp_us_ = event->timestamp_us;
            previous_input_timestamp_us_ = event->timestamp_us;
        }

        const std::uint64_t timestamp = static_cast<std::uint64_t>(
            event->timestamp_us - first_input_timestamp_us_);
        if (timestamp > maximum_timestamp_us)
            throw std::runtime_error("EBLITE recording exceeds the 41-bit timestamp range");

        const std::uint64_t delta = event_count_ == 0
                                        ? 0
                                        : static_cast<std::uint64_t>(
                                              event->timestamp_us -
                                              previous_input_timestamp_us_);
        const bool full = event_count_ == 0 || delta > time_payload_mask;
        const std::uint32_t time_payload = static_cast<std::uint32_t>(
            (full ? timestamp : delta) & time_payload_mask);
        const std::uint32_t first_word =
            (full ? full_event_bit : 0U) |
            (event->polarity ? polarity_bit : 0U) |
            (static_cast<std::uint32_t>(event->x) << x_shift) |
            (static_cast<std::uint32_t>(event->y) << y_shift) |
            time_payload;

        append_u32(first_word);
        if (full)
            append_u32(static_cast<std::uint32_t>(timestamp >> 9U));

        ++event_count_;
        recording_duration_us_ = timestamp;
        previous_input_timestamp_us_ = event->timestamp_us;
    }
}

void Writer::finalize() {
    if (finalized_)
        return;

    flush_buffer();
    output_.flush();
    if (!output_)
        throw std::runtime_error("could not flush EBLITE event stream: " +
                                 path_.string());

    Header header;
    header.recording_duration_us = recording_duration_us_;
    header.event_count = event_count_;
    header.sensor_width = sensor_width_;
    header.sensor_height = sensor_height_;
    const std::vector<std::uint8_t> encoded_header = encode_header(header);

    output_.seekp(0, std::ios::beg);
    output_.write(reinterpret_cast<const char*>(encoded_header.data()),
                  static_cast<std::streamsize>(encoded_header.size()));
    output_.flush();
    if (!output_)
        throw std::runtime_error("could not finalize EBLITE header: " +
                                 path_.string());
    output_.close();
    finalized_ = true;
}

} // namespace eblite
