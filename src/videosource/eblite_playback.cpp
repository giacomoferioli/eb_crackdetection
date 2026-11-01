#include "videosource/eblite_playback.hpp"

#include "eblite/format.hpp"
#include "utils/timing.hpp"

#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::size_t input_buffer_size = 4U * 1024U * 1024U;
constexpr std::uint32_t full_event_bit = std::uint32_t{1} << 31U;
constexpr std::uint32_t polarity_bit = std::uint32_t{1} << 30U;
constexpr std::uint32_t x_mask = 0x7FFU;
constexpr std::uint32_t y_mask = 0x3FFU;
constexpr std::uint32_t time_payload_mask = 0x1FFU;
constexpr unsigned x_shift = 19;
constexpr unsigned y_shift = 9;

} // namespace

class EblitePlayback::Impl {
public:
    explicit Impl(const std::filesystem::path& path)
        : input_buffer(input_buffer_size) {
        input.open(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("could not open EBLITE file: " + path.string());

        std::error_code error;
        const std::uint64_t file_size = std::filesystem::file_size(path, error);
        if (error)
            throw std::runtime_error("could not determine EBLITE file size: " +
                                     error.message());
        header = eblite::decode_header(input, file_size);
        input.clear();
        input.seekg(static_cast<std::streamoff>(header.event_stream_offset),
                    std::ios::beg);
        if (!input)
            throw std::runtime_error("could not seek to EBLITE event stream");
    }

    std::uint32_t read_word() {
        if (buffered_bytes - buffer_position < 4) {
            if (buffered_bytes != buffer_position)
                throw std::runtime_error("unaligned end of EBLITE event stream");
            input.read(reinterpret_cast<char*>(input_buffer.data()),
                       static_cast<std::streamsize>(input_buffer.size()));
            buffered_bytes = static_cast<std::size_t>(input.gcount());
            buffer_position = 0;
            if (buffered_bytes < 4)
                throw std::runtime_error("truncated EBLITE event stream at event " +
                                         std::to_string(decoded_events));
        }
        const std::uint32_t word =
            eblite::decode_u32_le(input_buffer.data() + buffer_position);
        buffer_position += 4;
        return word;
    }

    bool decode_next(Event& output) {
        if (decoded_events >= header.event_count)
            return false;

        const std::uint32_t first_word = read_word();
        const bool full = (first_word & full_event_bit) != 0;
        const std::uint64_t time_payload = first_word & time_payload_mask;
        std::uint64_t timestamp;
        if (full) {
            timestamp = (static_cast<std::uint64_t>(read_word()) << 9U) |
                        time_payload;
        } else {
            if (decoded_events == 0)
                throw std::runtime_error("first EBLITE event is not a full event");
            timestamp = file_timestamp_us + time_payload;
        }

        if (decoded_events == 0 && timestamp != 0)
            throw std::runtime_error("first EBLITE event timestamp is not zero");
        if (decoded_events != 0 && timestamp < file_timestamp_us)
            throw std::runtime_error("EBLITE event timestamps are not ordered");
        if (timestamp > eblite::maximum_timestamp_us)
            throw std::runtime_error("EBLITE event timestamp exceeds 41 bits");

        const std::uint16_t x = static_cast<std::uint16_t>(
            (first_word >> x_shift) & x_mask);
        const std::uint16_t y = static_cast<std::uint16_t>(
            (first_word >> y_shift) & y_mask);
        if (x >= header.sensor_width || y >= header.sensor_height)
            throw std::runtime_error("EBLITE event lies outside the sensor geometry");

        if (decoded_events == 0) {
            const std::uint64_t now = timing::us();
            if (now > static_cast<std::uint64_t>(
                          std::numeric_limits<std::int64_t>::max()))
                throw std::runtime_error("monotonic clock does not fit event timestamps");
            timestamp_offset_us = static_cast<std::int64_t>(now);
        }
        if (timestamp > static_cast<std::uint64_t>(
                            std::numeric_limits<std::int64_t>::max() -
                            timestamp_offset_us))
            throw std::runtime_error("EBLITE playback timestamp overflow");

        file_timestamp_us = timestamp;
        output = {timestamp_offset_us + static_cast<std::int64_t>(timestamp),
                  x,
                  y,
                  static_cast<std::uint8_t>((first_word & polarity_bit) != 0),
                  {0, 0, 0}};
        ++decoded_events;

        if (decoded_events == header.event_count &&
            file_timestamp_us != header.recording_duration_us)
            throw std::runtime_error(
                "EBLITE recording duration does not match its final event");
        return true;
    }

    std::vector<std::uint8_t> input_buffer;
    std::size_t buffer_position = 0;
    std::size_t buffered_bytes = 0;
    std::ifstream input;
    eblite::Header header;
    std::optional<Event> pending;
    std::uint64_t decoded_events = 0;
    std::uint64_t file_timestamp_us = 0;
    std::int64_t timestamp_offset_us = 0;
    bool stopped = false;
};

EblitePlayback::EblitePlayback(const std::filesystem::path& path)
    : impl_(std::make_unique<Impl>(path)) {}

EblitePlayback::~EblitePlayback() {
    stop();
}

std::size_t EblitePlayback::read_events(std::vector<Event>& output,
                                        std::int64_t until_timestamp_us) {
    output.clear();
    if (impl_->stopped)
        return 0;

    if (impl_->pending) {
        if (impl_->pending->timestamp_us > until_timestamp_us)
            return 0;
        output.push_back(*impl_->pending);
        impl_->pending.reset();
    }

    Event event{};
    while (impl_->decode_next(event)) {
        if (event.timestamp_us > until_timestamp_us) {
            impl_->pending = event;
            break;
        }
        output.push_back(event);
    }
    return output.size();
}

int EblitePlayback::width() const {
    return static_cast<int>(impl_->header.sensor_width);
}

int EblitePlayback::height() const {
    return static_cast<int>(impl_->header.sensor_height);
}

bool EblitePlayback::is_running() const {
    return !impl_->stopped &&
           (impl_->pending.has_value() ||
            impl_->decoded_events < impl_->header.event_count);
}

void EblitePlayback::stop() {
    if (!impl_ || impl_->stopped)
        return;
    impl_->stopped = true;
    impl_->pending.reset();
    impl_->input.close();
}

std::uint64_t EblitePlayback::event_count() const {
    return impl_->header.event_count;
}

std::uint64_t EblitePlayback::recording_duration_us() const {
    return impl_->header.recording_duration_us;
}
