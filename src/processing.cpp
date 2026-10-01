#include "processing.hpp"
#include "event_camera.hpp"
#include "image_saver.hpp"
#include "timing.hpp"
#include "constants.hpp"


#include <algorithm>
#include <cstdint>
#include <iostream>
#include <ostream>

Processing::Processing(Config cfg) :
    cfg(cfg),
    width(camera_.roi_width()) , height(camera_.roi_height()),
    event_queue(cfg.max_event_rate * 1LL * cfg.pseudoframe_length / 1'000'000),
    raw_frame(width * height,0),
    raw_img_saver(raw_frame,width,height, [] (std::int16_t value) {
            unsigned gray = std::clamp(128 + value * 64, 0, 255);
            return gray << 24U | gray << 16U | gray << 8U | 0xFF;
      })
{
    // Reused on every frame so draining the camera does not allocate.
    events_.reserve(cfg.processing_interval * 1LL * cfg.max_event_rate / 1'000'000);    
    
}

int Processing::run() {
    on_setup();
    
    //loop
    std::uint64_t last_save = 0;
    std::uint64_t last_proc_call = 0;
    //tempo timestamp + timeoffset = us()
    std::int64_t time_offset = 0;

    //pressing enter kills the program
    std::ios_base::sync_with_stdio(false);
    //wait for starting
    std::cout << "started" << std::endl;
    while ( std::cin.rdbuf()->in_avail() == 0) {
        
        uint64_t now = timing::us(); 
        //updates the pseudoframe and call process_frame
        if( now - last_proc_call > cfg.processing_interval ) {
            if ( now - last_proc_call > 2 * cfg.processing_interval)
                std::cout << "proc: can't keep up! took " << now - last_proc_call << std::endl;
            
            last_proc_call = timing::us();
            //updates pseudoframe
            camera_.read_events(events_);

            std::cout << 1e6 * (double)events_.size() / cfg.processing_interval << std::endl;

            //TODO: da fare meglio
            if(time_offset == 0 && events_.size() > 0 )
                time_offset = timing::us() - events_.back().timestamp_us;

            //removes old pixels
            while( !event_queue.empty() && now - ( time_offset + event_queue.peek().timestamp_us) > cfg.pseudoframe_length){
                //ci deve essere un modo migliore per sincronizzare i timing ma mi sfugge
                if(event_queue.count() == 1 && (int64_t)now - event_queue.peek().timestamp_us - time_offset < 0)
                    time_offset = timing::us() - event_queue.peek().timestamp_us;

                EventCamera::Event evt = event_queue.pop();
                raw_frame[evt.y * width + evt.x] -= (int8_t)evt.polarity * 2 - 1;
            }

            //oldest event first
            for( unsigned i = 0; i < events_.size(); i++){
                event_queue.append(events_[i]);
                raw_frame[events_[i].y * width + events_[i].x] += (int8_t)events_[i].polarity * 2 - 1;
            }
            

            process_frame();
        }

        now = timing::us();
        if( now - last_save > cfg.save_interval ){
            if(now - last_save > 2 * cfg.save_interval)
                std::cout << "save: can't keep up!" << std::endl;
            last_save = now;
            
            raw_img_saver.save_bitmap();
        }
    }
    
    //save camera settings
    if (camera_.is_open() && !camera_.save_settings(config::render::camera_settings_path)) {
        std::cerr << "Could not save camera settings: " << camera_.last_error() << '\n';
    }
    camera_.stop();
    
    return 0;
}
