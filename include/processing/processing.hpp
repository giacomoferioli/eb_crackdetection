#pragma once

#include "img_processing/frame.hpp"
#include "image_saver.hpp"
#include "specimen.hpp"
#include "utils/circular_buffer.hpp"
#include "videosource/event_source.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class EventCamera;


struct Config {
    bool realtime;
    std::string camera_config_path;
    std::string raw_save_path;
    double save_interval;
    int max_event_rate;
    double pseudoframe_length;
    double processing_interval;
    double saving_start, saving_end;
    double processing_start, processing_end;

    double camera_distance;
    float pixels_per_mm;
    float displacement_rate;
};

enum class ProcessResult {
    ERR,
    OK,
    DONE
};

// [R] Processing owns this typed handoff object for the lifetime of the whole
// processor pipeline. Producers publish successful results and later stages can
// read them without globals, files, or ownership transfers.
struct ProcessingData {
    std::optional<SpecimenPositionInfo> specimen_position;
    unsigned specimen_position_frame = 0;
};

// Derive from FrameProcessor and implement process_frame() to build an image
// processor.
// [R] Lifecycle hooks receive the first and last frame assigned to the
// processor, respectively.
class FrameProcessor {
public:
    explicit FrameProcessor(Config cfg) : cfg(cfg) {}
    virtual ~FrameProcessor() = default;

    FrameProcessor(const FrameProcessor&) = delete;
    FrameProcessor& operator=(const FrameProcessor&) = delete;

protected:
    virtual void on_setup(unsigned) {}
    virtual void on_termination(unsigned) {}
    virtual ProcessResult process_frame(unsigned framenum) = 0;

    const std::vector<Event>& events() const { return *events_; }
    
    int16_t PS(int x, int y) const { return (*raw_frame_)(x,y);}
    int16_t PS(int ix) const { return (*raw_frame_)[ix]; }

    BasicFrame<std::int16_t>& get_raw_frame() { return *raw_frame_; };

    // [R] The pointer is injected before on_setup(), so every active processor
    // can publish or consume typed results from earlier processing stages.
    ProcessingData& shared_data() { return *shared_data_; }
    const ProcessingData& shared_data() const { return *shared_data_; }

    Config cfg;
    int width = 0;
    int height = 0;

    std::unique_ptr<ImageSaver> img_saver;

private:
    friend class Processing;

    BasicFrame<std::int16_t>* raw_frame_ = nullptr;
    const std::vector<Event>* events_ = nullptr;
    ProcessingData* shared_data_ = nullptr;
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
    // [R] Interactive live-camera loop; unlike run(), its windows are controlled from stdin.
    int run_console(EventCamera& camera);

private:
    void setup_current_processor(unsigned framenum);
    void terminate_current_processor();
    FrameProcessor* process_frame(unsigned framenum);
    void stop_processing(const char* reason);

    EventSource& camera_;
    Config cfg;
    int width;
    int height;

    // [R] Declared before the processors so it outlives every processor object.
    ProcessingData shared_data_;
    std::vector<std::unique_ptr<FrameProcessor>> processors_;
    std::size_t processor_ix_ = 0;
    // [R] Remember the last frame actually handled by the active processor so
    // shutdown paths do not report a later, unprocessed scheduling frame.
    unsigned last_processed_framenum_ = 0;
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
