#pragma once

#include "videosource/event_source.hpp"
#include "image_saver.hpp"
#include "utils.hpp"

#include <cstdint>
#include <memory>
#include <vector>



struct Config {
    bool realtime;
    std::int64_t save_interval;
    int max_event_rate;
    std::int64_t pseudoframe_length;
    std::int64_t processing_interval;
};

// Derive from Processing and implement process_frame() to build an image
// processor. on_setup() is the place to add controls to settings().
class Processing {
public:
    Processing(EventSource& source, Config cfg);
    virtual ~Processing() = default;

    Processing(const Processing&) = delete;
    Processing& operator=(const Processing&) = delete;

    int run();

protected:
    virtual void on_setup() {}
    virtual void process_frame() = 0;

    const std::vector<Event>& events() const { return events_; }
    
    int16_t PS(int x, int y) const { return raw_frame[y * width + x ];}


private:
    EventSource& camera_;
protected:
    Config cfg;
    int width;
    int height;

    std::unique_ptr<ImageSaver<std::int16_t>> img_saver;

private:
    //circular buffer used to assemble the pseudoframe
    CircularBuffer<Event> event_queue;

    //pseudoframe
    std::vector<std::int16_t> raw_frame;
    ImageSaver<std::int16_t> raw_img_saver;
    
    //buffer for event reading
    std::vector<Event> events_;
};

class DummyProcessor final : public Processing {
    public:        
        DummyProcessor(EventSource& source, Config cfg) : Processing(source, cfg) {}

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
    file["realtime"] >> config.realtime;

    return config;
}
