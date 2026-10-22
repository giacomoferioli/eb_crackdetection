#include "processing/processing.hpp"
#include "image_saver.hpp"
#include "timing.hpp"
#include "videosource/event_camera.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ostream>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>

/*
 * This file coordinates the event-to-frame processing pipeline.
 *
 * The event source produces timestamped, signed pixel events. Processing::run()
 * places those events on a fixed timeline whose origin is the first event in
 * the stream. At every processing interval it maintains a rolling
 * "pseudoframe": a per-pixel sum of all polarities received during the last
 * cfg.pseudoframe_length seconds. A circular queue holds the same events so
 * their contributions can be removed when they leave that time window.
 *
 * Pseudoframes are assembled only while either processing or image saving can
 * use them. FrameProcessor instances run sequentially: a processor is set up
 * when it becomes current, receives frames until it returns DONE or ERR, and
 * is then terminated. In realtime mode the loop follows source timestamps and
 * treats missed frame deadlines as processing errors; offline mode advances as
 * quickly as the source permits. Raw and processor-specific diagnostic images
 * are saved independently inside the configured saving window.
 *
 * load_config() supplies the pipeline parameters from a strict key/value file:
 * unknown, duplicate, missing, and malformed settings are all rejected before
 * acquisition starts.
 */

Config load_config(const std::string& path) {
    std::ifstream file(path);
    if(!file)
        throw std::runtime_error("could not open config file: " + path);

    // Keep the setting identifier, its text name, and its presence flag aligned.
    enum Setting {
        REALTIME,
        CAMERA_CONFIG_PATH,
        RAW_SAVE_PATH,
        SAVE_INTERVAL,
        MAX_EVENT_RATE,
        PSEUDOFRAME_LENGTH,
        PROCESSING_INTERVAL,
        PROCESSING_START,
        PROCESSING_END,
        SAVING_START,
        SAVING_END,
        CAMERA_DISTANCE,
        PIXELS_PER_MM,
        DISPLACEMENT_RATE,
        SETTING_COUNT
    };

    const std::array<const char*,SETTING_COUNT> setting_names{
        "realtime", "camera_config_path", "raw_save_path",
        "save_interval", "max_event_rate", "pseudoframe_length",
        "processing_interval", "processing_start", "processing_end",
        "saving_start", "saving_end", "camera_distance",
        "pixels_per_mm","displacement_rate"
    };
    std::array<bool,SETTING_COUNT> found{};
    Config config{};

    std::string line;
    unsigned line_number = 0;
    while(std::getline(file,line)){
        // Ignore blank lines and comments, then split the remaining key/value pair.
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

        // These helpers give every setting the same duplicate and numeric checks.
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

        // Dispatch each recognized key into its strongly typed Config field.
        if(key == "realtime"){
            mark_found(REALTIME);
            if(value == "true")
                config.realtime = true;
            else if(value == "false")
                config.realtime = false;
            else
                throw std::runtime_error(path + ":" + std::to_string(line_number) + ": realtime must be true or false");
        }else if(key == "camera_config_path"){
            mark_found(CAMERA_CONFIG_PATH);
            config.camera_config_path = value;
        }else if(key == "raw_save_path"){
            mark_found(RAW_SAVE_PATH);
            config.raw_save_path = value;
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
        }else if(key == "camera_distance"){
            mark_found(CAMERA_DISTANCE);
            read_number(config.camera_distance);
        }else if(key == "pixels_per_mm"){
            mark_found(PIXELS_PER_MM);
            read_number(config.pixels_per_mm);
        }else if(key == "displacement_rate"){
            mark_found(DISPLACEMENT_RATE);
            read_number(config.displacement_rate);
        }else{
            throw std::runtime_error(path + ":" + std::to_string(line_number) + ": unknown setting: " + key);
        }
    }

    // A partially initialized configuration is never allowed to reach the loop.
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
    // Reuse the input batch storage so normal camera reads do not allocate.
    events_.reserve(cfg.processing_interval * cfg.max_event_rate);
}

void Processing::add_processor(std::unique_ptr<FrameProcessor> processor) {
    // Processor order defines the order in which processing stages become active.
    processors_.push_back(std::move(processor));
}

void Processing::setup_current_processor() {
    if(processor_running_ || processor_ix_ >= processors_.size())
        return;

    // Publish the shared frame/event state before invoking the stage hook.
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

    // The guard makes termination safe from every exit path in run().
    processors_[processor_ix_]->on_termination();
    processor_running_ = false;
}

FrameProcessor* Processing::process_frame(unsigned framenum) {
    // Lazily start the current stage on the first frame assigned to it.
    setup_current_processor();
    if(!processor_running_)
        return nullptr;

    FrameProcessor* processor = processors_[processor_ix_].get();
    ProcessResult result = processor->process_frame(framenum);

    // ERR stops the pipeline; DONE hands the next frame to the next stage.
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
    // Centralize error shutdown so the active processor is terminated once.
    terminate_current_processor();
    processing_stopped_ = true;
    processing_error_ = true;
    std::cout << "\nprocessing stopped: " << reason << std::endl;
}

int Processing::run() {
    std::int64_t run_start = timing::us();

    // A pending character on stdin is the user's stop signal for the main loop.
    std::ios_base::sync_with_stdio(false);
    std::cout << "started" << std::endl;

    // Wait for the first batch; its first timestamp anchors the fixed timeline.
    std::int64_t now;
    do {
        now = timing::us();
        camera_.read_events(events_, now);
    } while (camera_.is_running() && events_.empty());

    if(events_.empty()){
        camera_.stop();
        return 0;
    }

    // Convert configuration times once and keep scheduling arithmetic integral.
    const std::int64_t stream_start_us = events_.front().timestamp_us;
    const std::int64_t processing_interval_us = std::llround(cfg.processing_interval * 1e6);
    const std::int64_t pseudoframe_length_us = std::llround(cfg.pseudoframe_length * 1e6);
    const std::int64_t save_interval_us = std::llround(cfg.save_interval * 1e6);
    const std::int64_t processing_start_us = std::llround(cfg.processing_start * 1e6);
    const std::int64_t processing_end_us = std::llround(cfg.processing_end * 1e6);
    const std::int64_t saving_start_us = std::llround(cfg.saving_start * 1e6);
    const std::int64_t saving_end_us = std::llround(cfg.saving_end * 1e6);

    // Start assembling early enough that the first requested frame is complete.
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

    // Seed the rolling frame with the batch used to establish the time origin.
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

    // Use a power-of-two mask for inexpensive progress updates, roughly every 10 s.
    const unsigned print_progress_mask = ( 1 << (int)(std::ceil(std::log2( std::ceil(10 / cfg.processing_interval) ))) ) - 1;

    while (camera_.is_running() && std::cin.rdbuf()->in_avail() == 0) {
        // In realtime mode, wait until the wall clock reaches the next frame slot.
        const std::int64_t next_frame_time_us = (static_cast<std::int64_t>(framenum) + 1) * processing_interval_us;
        const std::int64_t next_frame_timestamp_us = stream_start_us + next_frame_time_us;
        if(cfg.realtime && static_cast<std::int64_t>(timing::us()) < next_frame_timestamp_us)
            continue;

        framenum++;
        frame_time_us = static_cast<std::int64_t>(framenum) * processing_interval_us;
        now = stream_start_us + frame_time_us;
        const double t = static_cast<double>(frame_time_us) / 1e6;
        if(!cfg.realtime && (framenum & print_progress_mask) == 0) std::cout << "\r" << t << "      " << std::flush;

        // Drain every event whose timestamp belongs to the newly reached slot.
        camera_.read_events(events_, now);

        // Enable or discard rolling-frame state as configured windows begin/end.
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

            // Subtract expired events so raw_frame always represents the window.
            while (!event_queue.empty() && now - event_queue.peek().timestamp_us > pseudoframe_length_us) {
                Event event = event_queue.pop();
                raw_frame(event.x,event.y) -= (int16_t)event.polarity * 2 - 1;
            }

            // Add the current batch to both the expiry queue and pixel sums.
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

        // Run the active processor only inside the configured processing window.
        FrameProcessor* processed_frame = nullptr;
        if(!processing_stopped_ && frame_time_us >= processing_start_us){
            if(frame_time_us >= processing_end_us){
                terminate_current_processor();
                processing_stopped_ = true;
            }else if(processor_ix_ >= processors_.size()){
                processing_stopped_ = true;
            }else{
                // A realtime stage must finish before the following frame slot.
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

        // Save at most once per interval and only while saving is enabled.
        if(frame_time_us - last_save_us >= save_interval_us &&
           frame_time_us >= saving_start_us && frame_time_us < saving_end_us) {
            last_save_us = frame_time_us;
            std::string filename = std::to_string(frame_time_us / 1000) + ".bmp";
            raw_img_saver.save_bitmap(filename);
            if(processed_frame != nullptr && processed_frame->img_saver != nullptr)
                processed_frame->img_saver->save_bitmap(filename);
        }
    }

    // Every normal exit closes the active stage and then stops the source.
    terminate_current_processor();
    camera_.stop();

    // Report how quickly offline execution advanced through simulated time.
    if(!cfg.realtime){
        double simulated_time = static_cast<double>(frame_time_us) / 1e6, real_time = (double)(timing::us() - run_start) / 1e6;
        std::cout << "\nprocessing ratio: " << simulated_time/ real_time << std::endl;
    }

    return processing_error_ ? 1 : 0;
}

/*
 * [R] Runs the live camera as a small state machine controlled from stdin. Events
 * are always drained from the camera, but they are added to the rolling
 * pseudoframe only while pseudoframe assembly is enabled. Processing can start
 * against the partial frame immediately or wait until one full pseudoframe
 * window has elapsed. Enabled pseudoframes and processor output are saved at
 * cfg.save_interval; EVT3 recording is controlled independently by the camera.
 */
int Processing::run_console(EventCamera& camera) {
    if(&camera != &camera_){
        std::cerr << "console camera is not the processing event source\n";
        return 1;
    }
    if(!camera.is_running()){
        std::cerr << "event camera is not running: " << camera.last_error() << '\n';
        return 1;
    }

    const std::int64_t processing_interval_us = std::llround(cfg.processing_interval * 1e6);
    const std::int64_t pseudoframe_length_us = std::llround(cfg.pseudoframe_length * 1e6);
    const std::int64_t save_interval_us = std::llround(cfg.save_interval * 1e6);
    if(processing_interval_us <= 0 || pseudoframe_length_us < 0 || save_interval_us <= 0){
        std::cerr << "processing_interval and save_interval must be positive; "
                     "pseudoframe_length cannot be negative\n";
        camera.stop();
        return 1;
    }

    bool assembling_pseudoframe = false;
    bool processing_enabled = false;
    bool waiting_for_pseudoframe = false;
    bool close_requested = false;
    std::int64_t pseudoframe_started_us = 0;
    std::int64_t next_processing_us = 0;
    std::int64_t last_save_us = 0;
    const std::int64_t console_started_us = timing::us();
    unsigned framenum = 0;
    FrameProcessor* output_processor = nullptr;
    std::filesystem::path recording_path;

    // [R] A start command clears every entry in its fixed image-output directory.
    const auto clear_image_directory = [](const std::filesystem::path& directory) {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if(error){
            std::cout << "Could not prepare " << directory.string() << ": "
                      << error.message() << '\n';
            return false;
        }

        std::filesystem::directory_iterator entry(directory, error);
        const std::filesystem::directory_iterator end;
        while(!error && entry != end){
            const std::filesystem::path path = entry->path();
            entry.increment(error);
            if(!error)
                std::filesystem::remove_all(path, error);
        }
        if(error){
            std::cout << "Could not clear " << directory.string() << ": "
                      << error.message() << '\n';
            return false;
        }
        std::cout << "Cleared " << directory.string() << ".\n";
        return true;
    };

    // [R] The complete two-digit command map is printed once when the console opens.
    const auto print_menu = [] {
        std::cout
            << "\n00 status\n"
            << "10 start RAW recording        11 stop RAW recording\n"
            << "20 start pseudoframe + saving 21 stop pseudoframe + saving\n"
            << "30 start processing + saving now\n"
            << "31 stop processing + saving\n"
            << "32 start processing + saving after full pseudoframe\n"
            << "40 start test (RAW + processing)  41 stop test\n"
            << "99 close\n> " << std::flush;
    };

    // [R] A fresh pseudoframe begins empty and starts a new image-save interval.
    const auto reset_pseudoframe = [&](std::int64_t now) {
        event_queue.clear();
        raw_frame.fill(0);
        assembling_pseudoframe = true;
        pseudoframe_started_us = now;
        last_save_us = now;
    };

    // [R] Processing depends on the pseudoframe, so stopping it also pauses processing.
    const auto stop_pseudoframe = [&] {
        processing_enabled = false;
        waiting_for_pseudoframe = false;
        output_processor = nullptr;
        event_queue.clear();
        raw_frame.fill(0);
        assembling_pseudoframe = false;
    };

    // [R] Processing clears its images, enables pseudoframes if needed, and then
    // either runs at once or waits for a complete rolling-window duration.
    const auto start_processing = [&](std::int64_t now, bool wait_for_full_frame) {
        if(processing_stopped_ || processor_ix_ >= processors_.size()){
            std::cout << "No processor is available.\n";
            return false;
        }
        if(!clear_image_directory("proc_images"))
            return false;
        if(!assembling_pseudoframe){
            if(!clear_image_directory("images"))
                return false;
            reset_pseudoframe(now);
        }

        const bool frame_is_full = now - pseudoframe_started_us >= pseudoframe_length_us;
        waiting_for_pseudoframe = wait_for_full_frame && !frame_is_full;
        processing_enabled = !waiting_for_pseudoframe;
        next_processing_us = now;
        std::cout << (waiting_for_pseudoframe
            ? "Processing will start when the pseudoframe is full.\n"
            : "Processing started.\n");
        return true;
    };

    // [R] Preserve earlier captures by adding _1, _2, and so on to an occupied path.
    const auto unused_recording_path = [&]() -> std::filesystem::path {
        const std::filesystem::path base(cfg.raw_save_path);
        std::error_code error;
        if(!std::filesystem::exists(base, error) && !error)
            return base;
        if(error)
            return {};

        for(unsigned suffix = 1; suffix != 0; ++suffix){
            const std::filesystem::path candidate =
                base.parent_path() /
                (base.stem().string() + "_" + std::to_string(suffix) + base.extension().string());
            if(!std::filesystem::exists(candidate, error) && !error)
                return candidate;
            if(error)
                return {};
        }
        return {};
    };

    // [R] RAW recording is independent from pseudoframe and processor state.
    const auto start_recording = [&] {
        if(camera.is_recording()){
            std::cout << "Recording is already running at " << recording_path.string() << ".\n";
            return true;
        }
        recording_path = unused_recording_path();
        if(recording_path.empty()){
            std::cout << "Could not select a free RAW recording path.\n";
            return false;
        }
        if(!camera.start_recording(recording_path.string())){
            std::cout << "Could not start recording: " << camera.last_error() << '\n';
            recording_path.clear();
            return false;
        }
        std::cout << "Recording started at " << recording_path.string() << ".\n";
        return true;
    };

    // [R] OpenEB finalizes the current RAW file when recording is stopped.
    const auto stop_recording = [&] {
        if(!camera.is_recording()){
            std::cout << "Recording is already stopped.\n";
            return;
        }
        if(!camera.stop_recording()){
            std::cout << "Could not stop recording: " << camera.last_error() << '\n';
            return;
        }
        std::cout << "Recording stopped.\n";
        recording_path.clear();
    };

    std::ios_base::sync_with_stdio(false);
    std::cout << "Camera console started.\n";
    print_menu();

    while(camera.is_running() && !close_requested){
        std::int64_t now = timing::us();

        // [R] Drain camera events continuously; retain them only for an enabled frame.
        camera_.read_events(events_, now);
        if(assembling_pseudoframe){
            while(!event_queue.empty() &&
                  now - event_queue.peek().timestamp_us > pseudoframe_length_us){
                const Event event = event_queue.pop();
                raw_frame(event.x,event.y) -= static_cast<std::int16_t>(event.polarity) * 2 - 1;
            }
            for(const Event& event : events_){
                event_queue.append(event);
                raw_frame(event.x,event.y) += static_cast<std::int16_t>(event.polarity) * 2 - 1;
            }
        }

        // [R] Promote a delayed start once the first complete rolling frame exists.
        if(waiting_for_pseudoframe &&
           now - pseudoframe_started_us >= pseudoframe_length_us){
            waiting_for_pseudoframe = false;
            processing_enabled = true;
            next_processing_us = now;
            std::cout << "\nPseudoframe is full; processing started.\n";
            std::cout << "> " << std::flush;
        }

        // [R] Run at the configured interval while the processing state is enabled.
        if(processing_enabled && now >= next_processing_us){
            ++framenum;
            output_processor = process_frame(framenum);
            next_processing_us = now + processing_interval_us;
            if(processing_stopped_){
                processing_enabled = false;
                output_processor = nullptr;
            }
        }

        // [R] Save computed raw and processor images together at cfg.save_interval.
        if(assembling_pseudoframe && now - last_save_us >= save_interval_us){
            last_save_us = now;
            const std::string filename =
                std::to_string((now - console_started_us) / 1000) + ".bmp";
            if(!raw_img_saver.save_bitmap(filename))
                std::cout << "\nCould not save pseudoframe: " << raw_img_saver.last_error() << '\n';
            if(processing_enabled && output_processor != nullptr &&
               output_processor->img_saver != nullptr &&
               !output_processor->img_saver->save_bitmap(filename))
                std::cout << "\nCould not save processor image: "
                          << output_processor->img_saver->last_error() << '\n';
        }

        // [R] Poll stdin briefly so acquisition continues while no command is entered.
        pollfd input{STDIN_FILENO, POLLIN, 0};
        const int input_ready = poll(&input, 1, 20);
        if(input_ready <= 0)
            continue;

        std::string line;
        if(!std::getline(std::cin, line)){
            close_requested = true;
            break;
        }
        std::istringstream command_stream(line);
        int command = -1;
        std::string extra;
        if(!(command_stream >> command) || command_stream >> extra){
            std::cout << "Invalid command.\n";
            std::cout << "> " << std::flush;
            continue;
        }

        // [R] Command handlers only change the three independent feature states.
        now = timing::us();
        switch(command){
        case 0:
            std::cout << "Recording: " << (camera.is_recording() ? "on" : "off")
                      << " | pseudoframe: " << (assembling_pseudoframe ? "on" : "off")
                      << " | processing: "
                      << (waiting_for_pseudoframe ? "waiting" : processing_enabled ? "on" : "off")
                      << "\n";
            break;
        case 10:
            start_recording();
            break;
        case 11:
            stop_recording();
            break;
        case 20:
            if(assembling_pseudoframe)
                std::cout << "Pseudoframe assembly is already running.\n";
            else if(clear_image_directory("images")){
                reset_pseudoframe(now);
                std::cout << "Pseudoframe assembly and saving started.\n";
            }
            break;
        case 21:
            if(!assembling_pseudoframe)
                std::cout << "Pseudoframe assembly is already stopped.\n";
            else{
                stop_pseudoframe();
                std::cout << "Pseudoframe assembly, processing, and image saving stopped.\n";
            }
            break;
        case 30:
            start_processing(now, false);
            break;
        case 31:
            processing_enabled = false;
            waiting_for_pseudoframe = false;
            output_processor = nullptr;
            std::cout << "Processing and processor-image saving stopped.\n";
            break;
        case 32:
            start_processing(now, true);
            break;
        case 40:
            if(start_recording()){
                if(clear_image_directory("images")){
                    reset_pseudoframe(now);
                    if(start_processing(now, true)){
                        std::cout << "Test started.\n";
                        break;
                    }
                }
                stop_pseudoframe();
                stop_recording();
                std::cout << "Test was not started.\n";
            }else{
                std::cout << "Test was not started.\n";
            }
            break;
        case 41:
            processing_enabled = false;
            waiting_for_pseudoframe = false;
            output_processor = nullptr;
            stop_pseudoframe();
            stop_recording();
            std::cout << "Test stopped.\n";
            break;
        case 99:
            close_requested = true;
            break;
        default:
            std::cout << "Unknown command.\n";
            break;
        }
        if(!close_requested)
            std::cout << "> " << std::flush;
    }

    // [R] Every exit finalizes the processor and RAW file before stopping the camera.
    terminate_current_processor();
    if(camera.is_recording() && !camera.stop_recording())
        std::cout << "Could not stop recording: " << camera.last_error() << '\n';
    camera.stop();
    std::cout << "Camera console closed.\n";
    return processing_error_ ? 1 : 0;
}
