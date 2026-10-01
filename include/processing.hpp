#pragma once

#include "event_camera.hpp"
#include "image_saver.hpp"
#include "utils.hpp"

#include <cstdint>
#include <vector>

struct Config {
    std::uint64_t save_interval;
    int max_event_rate;
    std::uint64_t pseudoframe_length;
    std::uint64_t processing_interval;
};

// Derive from Processing and implement process_frame() to build an image
// processor. on_setup() is the place to add controls to settings().
class Processing {
public:
    Processing(Config cfg);
    virtual ~Processing() = default;

    Processing(const Processing&) = delete;
    Processing& operator=(const Processing&) = delete;

    int run();

protected:
    virtual void on_setup() {}
    virtual void process_frame() = 0;

    // display() remains the processed output for backward compatibility.
    EventCamera& camera() { return camera_; }
    const std::vector<EventCamera::Event>& events() const { return events_; }
    
    int16_t PS(int x, int y) const { return raw_frame[y * width + x ];}


private:
    EventCamera camera_;
protected:
    Config cfg;
    int width;
    int height;

private:
    //circular buffer used to assemble the pseudoframe
    CircularBuffer<EventCamera::Event> event_queue;

    //pseudoframe
    std::vector<std::int16_t> raw_frame;

    
    //buffer for event reading
    std::vector<EventCamera::Event> events_;
    ImageSaver<std::int16_t> raw_img_saver;
};

class DummyProcessor final : public Processing {
    public:        
        DummyProcessor(Config cfg) : Processing(cfg) {}

    private:
        void process_frame() override {};

};

inline Config load_config(const std::string& path) {
    cv::FileStorage file(path, cv::FileStorage::READ);
    Config config;
    int value;

    value = file["save_interval"];
    config.save_interval = value;
    config.max_event_rate = file["max_event_rate"];
    value = file["pseudoframe_length"];
    config.pseudoframe_length = value;
    value = file["processing_interval"];
    config.processing_interval = value;

    return config;
}
