#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct Event {
    std::int64_t timestamp_us;
    std::uint16_t x;
    std::uint16_t y;
    std::uint8_t polarity;
    std::uint8_t reserved[3]{};
};

struct TriggerEvent {
    std::int64_t timestamp_us;
    std::int16_t channel_id;
    std::uint8_t polarity;
    std::uint8_t reserved[5]{};
};

class EventSource {
public:
    virtual ~EventSource() = default;

    virtual std::size_t read_events(std::vector<Event>& output, std::int64_t until_timestamp_us) = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual bool is_running() const = 0;
    virtual void stop() = 0;
};
