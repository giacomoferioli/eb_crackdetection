#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// High-throughput OpenEB camera wrapper. Events are copied by the OpenEB
// callback into a bounded SPSC ring, keeping camera decoding independent from
// application processing. One thread may read while the camera callback writes.
class EventCamera {
public:
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

    struct BiasSetting {
        std::string name;
        int value;
        int minimum;
        int maximum;
        bool modifiable;
    };

    struct RoiSetting {
        int x;
        int y;
        int width;
        int height;
        bool enabled;
    };

    // Written by OpenEB's trigger callback. -1 means no rising trigger has
    // been received since the camera was opened.
    std::atomic<std::int64_t> last_rising_trigger_timestamp_us{-1};

    // The default ring stores roughly four million events (64 MiB). Storage is
    // allocated only when a camera is opened.
    explicit EventCamera(std::size_t event_capacity = 1U << 22U);
    ~EventCamera();

    EventCamera(const EventCamera&) = delete;
    EventCamera& operator=(const EventCamera&) = delete;

    bool open_first();
    bool open_usb(std::uint32_t device_index = 0);
    bool open_serial(const std::string& serial);
    void close();
    bool start();
    void stop();

    bool is_open() const;
    bool is_running() const;
    int width() const;
    int height() const;
    int roi_width() const;
    int roi_height() const;

    // Fastest read API: copies up to max_events into caller-owned memory and
    // returns the number copied. Call this regularly from the processing loop.
    std::size_t read_events(Event* destination, std::size_t max_events) noexcept;

    // Convenience API. Reuses output capacity, but callers should reserve a
    // suitable size once during setup to guarantee allocation-free reads.
    std::size_t read_events(std::vector<Event>& output,
                            std::size_t max_events = 0);
    std::size_t pending_events() const noexcept;
    std::size_t event_capacity() const noexcept;
    std::uint64_t dropped_events() const noexcept;
    void clear_events() noexcept;

    std::size_t read_trigger_events(TriggerEvent* destination, std::size_t max_events) noexcept;
    std::size_t read_trigger_events(std::vector<TriggerEvent>& output, std::size_t max_events = 0);
    std::size_t pending_trigger_events() const noexcept;
    std::uint64_t dropped_trigger_events() const noexcept;
    void clear_trigger_events() noexcept;
    bool trigger_input_enabled() const noexcept;

    bool set_bias(const std::string& name, int value);
    std::optional<int> bias(const std::string& name) const;
    std::map<std::string, int> biases() const;
    std::vector<BiasSetting> bias_settings() const;

    bool load_settings(const std::string& path);
    bool save_settings(const std::string& path) const;

    // Configures and immediately enables a rectangular hardware ROI.
    bool set_roi(int x, int y, int width, int height);
    bool enable_roi(bool enabled);
    std::optional<RoiSetting> roi_setting() const noexcept;
    void set_roi_coordinates_local(bool enabled) noexcept;
    bool roi_coordinates_local() const noexcept;

    std::string last_error() const;
    static bool openeb_available() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

static_assert(sizeof(EventCamera::Event) == 16, "EventCamera::Event must remain compact");
static_assert(sizeof(EventCamera::TriggerEvent) == 16, "EventCamera::TriggerEvent must remain compact");
