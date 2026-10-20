#include "processing/processing.hpp"
#include "image_saver.hpp"
#include "timing.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

Config load_config(const std::string& path) {
    std::ifstream file(path);
    if(!file)
        throw std::runtime_error("could not open config file: " + path);

    enum Setting {
        REALTIME,
        SAVE_INTERVAL,
        MAX_EVENT_RATE,
        PSEUDOFRAME_LENGTH,
        PROCESSING_INTERVAL,
        PROCESSING_START,
        PROCESSING_END,
        SAVING_START,
        SAVING_END,
        SETTING_COUNT
    };

    const std::array<const char*,SETTING_COUNT> setting_names{
        "realtime", "save_interval", "max_event_rate", "pseudoframe_length",
        "processing_interval", "processing_start", "processing_end",
        "saving_start", "saving_end"
    };
    std::array<bool,SETTING_COUNT> found{};
    Config config{};

    std::string line;
    unsigned line_number = 0;
    while(std::getline(file,line)){
        line_number++;
        const std::size_t first = line.find_first_not_of(" \t\r");
        if(first == std::string::npos || line[first] == '#')
            continue;

        const std::size_t equals = line.find('=',first);
        if(equals == std::string::npos || equals == first)
            throw std::runtime_error(path + ":" + std::to_string(line_number) + ": expected key = value");

        const std::size_t key_end = line.find_last_not_of(" \t\r",equals - 1);
        const std::size_t value_start = line.find_first_not_of(" \t\r",equals + 1);
        if(key_end == std::string::npos || value_start == std::string::npos)
            throw std::runtime_error(path + ":" + std::to_string(line_number) + ": expected key = value");

        const std::size_t value_end = line.find_last_not_of(" \t\r");
        const std::string key = line.substr(first,key_end - first + 1);
        const std::string value = line.substr(value_start,value_end - value_start + 1);

        auto mark_found = [&](Setting setting) {
            if(found[setting])
                throw std::runtime_error(path + ":" + std::to_string(line_number) + ": duplicate setting: " + key);
            found[setting] = true;
        };
        auto read_number = [&](auto& destination) {
            std::istringstream input(value);
            if(!(input >> destination))
                throw std::runtime_error(path + ":" + std::to_string(line_number) + ": invalid value for: " + key);
            input >> std::ws;
            if(!input.eof())
                throw std::runtime_error(path + ":" + std::to_string(line_number) + ": invalid value for: " + key);
        };

        if(key == "realtime"){
            mark_found(REALTIME);
            if(value == "true")
                config.realtime = true;
            else if(value == "false")
                config.realtime = false;
            else
                throw std::runtime_error(path + ":" + std::to_string(line_number) + ": realtime must be true or false");
        }else if(key == "save_interval"){
            mark_found(SAVE_INTERVAL);
            read_number(config.save_interval);
        }else if(key == "max_event_rate"){
            mark_found(MAX_EVENT_RATE);
            read_number(config.max_event_rate);
        }else if(key == "pseudoframe_length"){
            mark_found(PSEUDOFRAME_LENGTH);
            read_number(config.pseudoframe_length);
        }else if(key == "processing_interval"){
            mark_found(PROCESSING_INTERVAL);
            read_number(config.processing_interval);
        }else if(key == "processing_start"){
            mark_found(PROCESSING_START);
            read_number(config.processing_start);
        }else if(key == "processing_end"){
            mark_found(PROCESSING_END);
            read_number(config.processing_end);
        }else if(key == "saving_start"){
            mark_found(SAVING_START);
            read_number(config.saving_start);
        }else if(key == "saving_end"){
            mark_found(SAVING_END);
            read_number(config.saving_end);
        }else{
            throw std::runtime_error(path + ":" + std::to_string(line_number) + ": unknown setting: " + key);
        }
    }

    for(std::size_t i = 0; i < found.size(); i++)
        if(!found[i])
            throw std::runtime_error(path + ": missing setting: " + setting_names[i]);

    return config;
}

Processing::Processing(EventSource& source, Config cfg) :
    camera_(source),
    cfg(cfg),
    width(camera_.width()) , height(camera_.height()),
    event_queue(cfg.max_event_rate * cfg.pseudoframe_length),
    raw_frame(width,height,0),
    raw_img_saver([this] (int ix) {
            unsigned gray = std::clamp(128 + raw_frame[ix] * 16, 0, 255);
            return gray << 24U | gray << 16U | gray << 8U | 0xFF;
      }, width,height,"images/")
{
    // Reused on every frame so draining the camera does not allocate.
    events_.reserve(cfg.processing_interval * cfg.max_event_rate);
}

void Processing::add_processor(std::unique_ptr<FrameProcessor> processor) {
    processors_.push_back(std::move(processor));
}

void Processing::setup_current_processor() {
    if(processor_running_ || processor_ix_ >= processors_.size())
        return;

    FrameProcessor& processor = *processors_[processor_ix_];
    processor.width = width;
    processor.height = height;
    processor.raw_frame_ = &raw_frame;
    processor.events_ = &events_;
    processor.on_setup();
    processor_running_ = true;
}

void Processing::terminate_current_processor() {
    if(!processor_running_)
        return;

    processors_[processor_ix_]->on_termination();
    processor_running_ = false;
}

FrameProcessor* Processing::process_frame(unsigned framenum) {
    setup_current_processor();
    if(!processor_running_)
        return nullptr;

    FrameProcessor* processor = processors_[processor_ix_].get();
    ProcessResult result = processor->process_frame(framenum);

    if(result == ProcessResult::ERR){
        stop_processing("processor returned ERR");
    }else if(result == ProcessResult::DONE){
        terminate_current_processor();
        processor_ix_++;
        if(processor_ix_ >= processors_.size())
            processing_stopped_ = true;
    }

    return processor;
}

void Processing::stop_processing(const char* reason) {
    terminate_current_processor();
    processing_stopped_ = true;
    processing_error_ = true;
    std::cout << "\nprocessing stopped: " << reason << std::endl;
}

int Processing::run() {
    std::int64_t run_start = timing::us();

    //pressing enter kills the program
    std::ios_base::sync_with_stdio(false);
    std::cout << "started" << std::endl;

    // The source aligns its first event with this clock. Its timestamp is the
    // origin of the fixed frame timeline.
    std::int64_t now;
    do {
        now = timing::us();
        camera_.read_events(events_, now);
    } while (camera_.is_running() && events_.empty());

    if(events_.empty()){
        camera_.stop();
        return 0;
    }

    const std::int64_t stream_start_us = events_.front().timestamp_us;
    const std::int64_t processing_interval_us = std::llround(cfg.processing_interval * 1e6);
    const std::int64_t pseudoframe_length_us = std::llround(cfg.pseudoframe_length * 1e6);
    const std::int64_t save_interval_us = std::llround(cfg.save_interval * 1e6);
    const std::int64_t processing_start_us = std::llround(cfg.processing_start * 1e6);
    const std::int64_t processing_end_us = std::llround(cfg.processing_end * 1e6);
    const std::int64_t saving_start_us = std::llround(cfg.saving_start * 1e6);
    const std::int64_t saving_end_us = std::llround(cfg.saving_end * 1e6);

    const auto window_needs_pseudoframe = [pseudoframe_length_us](std::int64_t t, std::int64_t start, std::int64_t end) {
        return start < end && t >= std::max<std::int64_t>(0,start - pseudoframe_length_us) && t < end;
    };
    const auto pseudoframe_needed = [&](std::int64_t t) {
        const bool processing_needs_it =
            !processing_stopped_ && processor_ix_ < processors_.size() &&
            window_needs_pseudoframe(t,processing_start_us,processing_end_us);
        const bool saving_needs_it =
            window_needs_pseudoframe(t,saving_start_us,saving_end_us);
        return processing_needs_it || saving_needs_it;
    };

    bool assembling_pseudoframe = pseudoframe_needed(0);
    if(assembling_pseudoframe){
        for(const Event& event : events_){
            event_queue.append(event);
            raw_frame(event.x,event.y) += (int8_t)event.polarity * 2 - 1;
        }
    }

    unsigned framenum = 0;
    std::int64_t frame_time_us = 0;
    std::int64_t last_save_us = 0;

    //this mask is assembled so that progress is printed ca. every 10 simulated seconds, trust the math
    const unsigned print_progress_mask = ( 1 << (int)(std::ceil(std::log2( std::ceil(10 / cfg.processing_interval) ))) ) - 1;

    while (camera_.is_running() && std::cin.rdbuf()->in_avail() == 0) {
        const std::int64_t next_frame_time_us = (static_cast<std::int64_t>(framenum) + 1) * processing_interval_us;
        const std::int64_t next_frame_timestamp_us = stream_start_us + next_frame_time_us;
        if(cfg.realtime && static_cast<std::int64_t>(timing::us()) < next_frame_timestamp_us)
            continue;

        framenum++;
        frame_time_us = static_cast<std::int64_t>(framenum) * processing_interval_us;
        now = stream_start_us + frame_time_us;
        const double t = static_cast<double>(frame_time_us) / 1e6;
        if(!cfg.realtime && (framenum & print_progress_mask) == 0) std::cout << "\r" << t << "      " << std::flush;

        camera_.read_events(events_, now);

        const bool needs_pseudoframe = pseudoframe_needed(frame_time_us);
        if(!needs_pseudoframe && assembling_pseudoframe){
            event_queue.clear();
            raw_frame.fill(0);
            assembling_pseudoframe = false;
        }else if(needs_pseudoframe){
            if(!assembling_pseudoframe){
                event_queue.clear();
                raw_frame.fill(0);
                assembling_pseudoframe = true;
            }

            // Remove events that have left the pseudoframe time window.
            while (!event_queue.empty() && now - event_queue.peek().timestamp_us > pseudoframe_length_us) {
                Event event = event_queue.pop();
                raw_frame(event.x,event.y) -= (int16_t)event.polarity * 2 - 1;
            }

            //adds new events
            for (const Event& event : events_) {
                try{
                    event_queue.append(event);
                }catch (const std::exception& e){
                    std::cout << "\n" << event_queue.peek().timestamp_us << "\n" << event.timestamp_us << std::endl;
                    throw;
                }
                raw_frame(event.x,event.y) += (int16_t)event.polarity * 2 - 1;
            }
        }

        FrameProcessor* processed_frame = nullptr;
        if(!processing_stopped_ && frame_time_us >= processing_start_us){
            if(frame_time_us >= processing_end_us){
                terminate_current_processor();
                processing_stopped_ = true;
            }else if(processor_ix_ >= processors_.size()){
                processing_stopped_ = true;
            }else{
                const std::int64_t processing_deadline_us = now + processing_interval_us;
                if(cfg.realtime && static_cast<std::int64_t>(timing::us()) >= processing_deadline_us){
                    stop_processing("frame deadline was already missed");
                }else{
                    processed_frame = process_frame(framenum);
                    if(!processing_error_ && cfg.realtime &&
                       static_cast<std::int64_t>(timing::us()) >= processing_deadline_us)
                        stop_processing("frame processor missed its deadline");
                }
            }
        }

        if(frame_time_us - last_save_us >= save_interval_us &&
           frame_time_us >= saving_start_us && frame_time_us < saving_end_us) {
            last_save_us = frame_time_us;
            std::string filename = std::to_string(frame_time_us / 1000) + ".bmp";
            raw_img_saver.save_bitmap(filename);
            if(processed_frame != nullptr && processed_frame->img_saver != nullptr)
                processed_frame->img_saver->save_bitmap(filename);
        }
    }

    terminate_current_processor();
    camera_.stop();

    if(!cfg.realtime){
        double simulated_time = static_cast<double>(frame_time_us) / 1e6, real_time = (double)(timing::us() - run_start) / 1e6;
        std::cout << "\nprocessing ratio: " << simulated_time/ real_time << std::endl;
    }

    return processing_error_ ? 1 : 0;
}
