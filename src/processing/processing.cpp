#include "processing/processing.hpp"
#include "image_saver.hpp"
#include "timing.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <ostream>
#include <string>

Processing::Processing(EventSource& source, Config cfg) :
    camera_(source),
    cfg(cfg),
    width(camera_.width()) , height(camera_.height()),
    event_queue(cfg.max_event_rate * 1LL * cfg.pseudoframe_length / 1'000'000),
    raw_frame(width * height,0),
    raw_img_saver(raw_frame,width,height,"images/", [] (std::int16_t value) {
            unsigned gray = std::clamp(128 + value * 32, 0, 255);
            return gray << 24U | gray << 16U | gray << 8U | 0xFF;
      })
{
    // Reused on every frame so draining the camera does not allocate.
    events_.reserve(cfg.processing_interval * 1LL * cfg.max_event_rate / 1'000'000);     
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

    std::int64_t last_save = now;
    std::int64_t last_proc_call = now;
    
    while (camera_.is_running() && std::cin.rdbuf()->in_avail() == 0) {
        if (cfg.realtime) {
            now = timing::us();
            if (now - last_proc_call < cfg.processing_interval) continue;
            if (now - last_proc_call > 2 * cfg.processing_interval)
                std::cout << "proc: can't keep up! took " << now - last_proc_call << std::endl;
        } else {
            now += cfg.processing_interval;
        }
        last_proc_call = now;

        camera_.read_events(events_, now);

        // Remove events that have left the pseudoframe time window.
        while (!event_queue.empty() && now - event_queue.peek().timestamp_us > cfg.pseudoframe_length) {
            Event event = event_queue.pop();
            raw_frame[event.y * width + event.x] -= (int8_t)event.polarity * 2 - 1;
        }

        for (const Event& event : events_) {
            event_queue.append(event);
            raw_frame[event.y * width + event.x] += (int8_t)event.polarity * 2 - 1;
        }

        process_frame();

        if (now - last_save >= cfg.save_interval) {
            if (cfg.realtime && now - last_save > 2 * cfg.save_interval)
                std::cout << "save: can't keep up!" << std::endl;
            last_save = now;
            if(cfg.realtime){
                raw_img_saver.save_bitmap();
                if(img_saver != nullptr)
                    img_saver->save_bitmap();
            }else{
                std::string filename = std::to_string((int)(now-run_start)/1000) + ".bmp";
                raw_img_saver.save_bitmap(filename);
                if(img_saver != nullptr){
                    img_saver->save_bitmap(filename);
                }
            }
        }
    }

    camera_.stop();

    if(!cfg.realtime){
        double simulated_time = (double)(now - run_start) / 1e6, real_time = (double)(timing::us() - run_start) / 1e6;
        std::cout << "processing ratio: " << simulated_time/ real_time << std::endl;
    }

    return 0;
}

