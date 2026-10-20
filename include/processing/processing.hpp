#pragma once

#include "img_processing/frame.hpp"
#include "videosource/event_source.hpp"
#include "image_saver.hpp"
#include "utils.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>



struct Config {
    bool realtime;
    double save_interval;
    int max_event_rate;
    double pseudoframe_length;
    double processing_interval;
    double saving_start, saving_end;
    double processing_start, processing_end;
};

enum class ProcessResult {
    ERR,
    OK,
    DONE
};

// Derive from FrameProcessor and implement process_frame() to build an image
// processor. on_setup() is called when the processor becomes active.
class FrameProcessor {
public:
    explicit FrameProcessor(Config cfg) : cfg(cfg) {}
    virtual ~FrameProcessor() = default;

    FrameProcessor(const FrameProcessor&) = delete;
    FrameProcessor& operator=(const FrameProcessor&) = delete;

protected:
    virtual void on_setup() {}
    virtual void on_termination() {}
    virtual ProcessResult process_frame(unsigned framenum) = 0;

    const std::vector<Event>& events() const { return *events_; }
    
    int16_t PS(int x, int y) const { return (*raw_frame_)(x,y);}
    int16_t PS(int ix) const { return (*raw_frame_)[ix]; }

    BasicFrame<std::int16_t>& get_raw_frame() { return *raw_frame_; };

    Config cfg;
    int width = 0;
    int height = 0;

    std::unique_ptr<ImageSaver> img_saver;

private:
    friend class Processing;

    BasicFrame<std::int16_t>* raw_frame_ = nullptr;
    const std::vector<Event>* events_ = nullptr;
};

// Owns the event source and pseudoframe. Frame processors run in insertion
// order; returning DONE activates the next processor on the following frame.
class Processing {
public:
    Processing(EventSource& source, Config cfg);
    ~Processing() = default;

    Processing(const Processing&) = delete;
    Processing& operator=(const Processing&) = delete;

    void add_processor(std::unique_ptr<FrameProcessor> processor);
    int run();

private:
    void setup_current_processor();
    void terminate_current_processor();
    FrameProcessor* process_frame(unsigned framenum);
    void stop_processing(const char* reason);

    EventSource& camera_;
    Config cfg;
    int width;
    int height;

    std::vector<std::unique_ptr<FrameProcessor>> processors_;
    std::size_t processor_ix_ = 0;
    bool processor_running_ = false;
    bool processing_stopped_ = false;
    bool processing_error_ = false;

    //circular buffer used to assemble the pseudoframe
    CircularBuffer<Event> event_queue;

    //pseudoframe
    BasicFrame<std::int16_t> raw_frame;
    ImageSaver raw_img_saver;
    
    //buffer for event reading
    std::vector<Event> events_;
};

class DummyProcessor final : public FrameProcessor {
    public:        
        explicit DummyProcessor(Config cfg) : FrameProcessor(cfg) {}

    private:
        ProcessResult process_frame(unsigned) override { return ProcessResult::OK; };

};

Config load_config(const std::string& path);
