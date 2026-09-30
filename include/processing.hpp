#pragma once

#include "display_window.hpp"
#include "event_camera.hpp"
#include "event_source.hpp"
#include "settings_window.hpp"
#include "utils.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Derive from Processing and implement process_frame() to build an image
// processor. on_setup() is the place to add controls to settings().
class Processing {
public:
    Processing(int width, int height, std::string title = "Image display",std::size_t settings_columns = 3);
    virtual ~Processing() = default;

    Processing(const Processing&) = delete;
    Processing& operator=(const Processing&) = delete;

    int run();

    virtual void update_size(int width, int height);

protected:
    virtual void on_setup() {}
    virtual void process_frame() = 0;

    // display() remains the processed output for backward compatibility.
    DisplayWindow& display() { return processed_display_; }
    DisplayWindow& raw_display() { return raw_display_; }
    SettingsWindow& settings() { return settings_; }
    EventCamera& camera() { return camera_; }
    const std::vector<EventCamera::Event>& events() const { return events_; }
    
    int16_t PS(int x, int y) const { return raw_frame[y * width + x ];}

    int width;
    int height;
    std::vector<std::int16_t> frame;

private:
    //circular buffer used to assemble the pseudoframe
    
    CircularBuffer<EventCamera::Event> event_queue;

    std::vector<std::int16_t> raw_frame;

    std::string title_;
    std::size_t settings_columns_;
    DisplayWindow raw_display_;
    DisplayWindow processed_display_;
    EventCamera camera_;
    EventSource event_source_;
    SettingsWindow settings_;
    std::vector<EventCamera::Event> events_;
    
};
