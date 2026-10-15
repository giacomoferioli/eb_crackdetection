#pragma once

#include "videosource/event_source.hpp"
#include "image_saver.hpp"
#include "utils.hpp"

#include <cstdint>
#include <memory>
#include <vector>



struct Config {
    bool realtime;
    double save_interval;
    int max_event_rate;
    double pseudoframe_length;
    double processing_interval;
    double saving_start, saving_end, analysis_start;
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
    virtual void on_termination() {}
    virtual void process_frame(unsigned framenum) = 0;

    const std::vector<Event>& events() const { return events_; }
    
    int16_t PS(int x, int y) const { return raw_frame[y * width + x ];}
    int16_t PS(int ix) const { return  raw_frame[ix]; }

    std::vector<std::int16_t>& get_raw_frame() { return raw_frame;};

private:
    EventSource& camera_;
protected:
    Config cfg;
    int width;
    int height;

    std::unique_ptr<ImageSaver> img_saver;

private:
    //circular buffer used to assemble the pseudoframe
    CircularBuffer<Event> event_queue;

    //pseudoframe
    std::vector<std::int16_t> raw_frame;
    ImageSaver raw_img_saver;
    
    //buffer for event reading
    std::vector<Event> events_;
};

class DummyProcessor final : public Processing {
    public:        
        DummyProcessor(EventSource& source, Config cfg) : Processing(source, cfg) {}

    private:
        void process_frame(unsigned) override {};

};

inline Config load_config(const std::string& path) {
    cv::FileStorage file(path, cv::FileStorage::READ);
    Config config;

    file["save_interval"] >> config.save_interval;
    config.max_event_rate = file["max_event_rate"];
    file["pseudoframe_length"] >> config.pseudoframe_length;
    file["processing_interval"] >> config.processing_interval;
    file["realtime"] >> config.realtime;

    file["saving_start"] >> config.saving_start;
    file["saving_end"] >> config.saving_end;
    file["analysis_start"] >> config.analysis_start;
    return config;
}
