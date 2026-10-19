#include "processing/processing.hpp"
#include "image_saver.hpp"
#include "timing.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <ostream>
#include <string>

Processing::Processing(EventSource& source, Config cfg) :
    camera_(source),
    cfg(cfg),
    width(camera_.width()) , height(camera_.height()),
    event_queue(cfg.max_event_rate * cfg.pseudoframe_length),
    raw_frame(width * height,0),
    raw_img_saver([this] (int ix) {
            unsigned gray = std::clamp(128 + raw_frame[ix] * 16, 0, 255);
            return gray << 24U | gray << 16U | gray << 8U | 0xFF;
      }, width,height,"images/")
{
    // Reused on every frame so draining the camera does not allocate.
    events_.reserve(cfg.processing_interval * cfg.max_event_rate);
}

int Processing::run() {
    on_setup();
    std::int64_t run_start = timing::us();

    //pressing enter kills the program
    std::ios_base::sync_with_stdio(false);
    std::cout << "started" << std::endl;

    // The source aligns its first event with this clock.
    std::int64_t now;
    do {
        now = timing::us();
        camera_.read_events(events_, now);
    } while (camera_.is_running() && events_.empty());

    for (const Event& event : events_) {
        event_queue.append(event);
        raw_frame[event.y * width + event.x] += (int8_t)event.polarity * 2 - 1;
    }

    unsigned framenum = 0;

    std::int64_t last_save = now;
    std::int64_t last_proc_call = now;
    const std::int64_t processing_interval_us = std::llround(cfg.processing_interval * 1e6);
    const std::int64_t pseudoframe_length_us = std::llround(cfg.pseudoframe_length * 1e6);
    const std::int64_t save_interval_us = std::llround(cfg.save_interval * 1e6);

    //this mask is assembled so that progress is printed ca. every 10 simulated seconds, trust the math
    const unsigned print_progress_mask = ( 1 << (int)(std::ceil(std::log2( std::ceil(10 / cfg.processing_interval) ))) ) - 1;

    while (camera_.is_running() && std::cin.rdbuf()->in_avail() == 0) {
        if (cfg.realtime) {
            now = timing::us();
            if (now - last_proc_call < processing_interval_us) continue;
            if (now - last_proc_call > 2 * processing_interval_us)
                std::cout << "proc: can't keep up! took " << now - last_proc_call << std::endl;
        } else {
            now += processing_interval_us;
        }
        last_proc_call = now;
        const double t = ++framenum * cfg.processing_interval;
        if(!cfg.realtime && (framenum & print_progress_mask) == 0) std::cout << "\r" << t << "      " << std::flush;

        camera_.read_events(events_, now);
        

        //starts assembying pseudoframe
        if( t < cfg.analysis_start - cfg.pseudoframe_length)
            continue;

        // Remove events that have left the pseudoframe time window.
        while (!event_queue.empty() && now - event_queue.peek().timestamp_us > pseudoframe_length_us) {
            Event event = event_queue.pop();
            //int y = std::max(0.,std::round(event.y - SINKRATE * event.timestamp_us));
            raw_frame[event.y * width + event.x] -= (int8_t)event.polarity * 2 - 1;
        }

        for (const Event& event : events_) {
            try{
                event_queue.append(event);
            }catch (const std::exception& e){
                std::cout << "\n" << event_queue.peek().timestamp_us << "\n" << event.timestamp_us << std::endl;
                throw;
            }
            //int y = std::max(0.,std::round(event.y - SINKRATE * event.timestamp_us));
            raw_frame[event.y * width + event.x] += (int8_t)event.polarity * 2 - 1;
        }

        if( t < cfg.analysis_start )
            continue;

        process_frame(framenum);

        if (now - last_save >= save_interval_us && (t > cfg.saving_start && t < cfg.saving_end)) {
            if (cfg.realtime && now - last_save > 2 * save_interval_us)
                std::cout << "save: can't keep up!" << std::endl;
            last_save = now;
            if(cfg.realtime){
                raw_img_saver.save_bitmap();
                if(img_saver != nullptr)
                    img_saver->save_bitmap();
            }else{
                std::string filename = std::to_string((now-run_start)/1000) + ".bmp";
                raw_img_saver.save_bitmap(filename);
                if(img_saver != nullptr){
                    img_saver->save_bitmap(filename);
                }
            }
        }
    }

    on_termination();
    camera_.stop();

    if(!cfg.realtime){
        double simulated_time = (double)(now - run_start) / 1e6, real_time = (double)(timing::us() - run_start) / 1e6;
        std::cout << "\nprocessing ratio: " << simulated_time/ real_time << std::endl;
    }

    return 0;
}
