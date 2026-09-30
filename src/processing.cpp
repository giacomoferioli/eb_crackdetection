#include "processing.hpp"
#include "event_camera.hpp"
#include "timing.hpp"
#include "constants.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <ostream>
#include <utility>

Processing::Processing(int width, int height, std::string title,std::size_t settings_columns)
    : width(width), height(height), frame(width * height,0),
      event_queue(config::inputdata::max_event_rate * config::inputdata::pseudoframe_length / 1'000'000),
      raw_frame(width * height,0),
      title_(std::move(title)),
      settings_columns_(std::max<std::size_t>(3, settings_columns)),
      event_source_(camera_),
      settings_(raw_display_, processed_display_, camera_, event_source_, [this](int width, int height) { update_size(width, height); })
{
    // Reused on every frame so draining the camera does not allocate.
    events_.reserve(1'000'000);    
}


void Processing::update_size(int width, int height){
    this->width = width;
    this->height = height;

    raw_frame.resize(width * height);
    frame.resize(width * height);

    std::fill(raw_frame.begin(),raw_frame.end(),0);
    std::fill(frame.begin(),frame.end(),0);
}

int Processing::run() {
    if (width <= 0 || height <= 0) {
        std::cerr << "Invalid image dimensions\n";
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
        return 1;
    }
    if (!raw_display_.create(title_ + " - Raw", width, height)) {
        std::cerr << "Raw display window creation failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
    if (!processed_display_.create(title_ + " - Processed", width, height)) {
        std::cerr << "Processed display window creation failed: " << SDL_GetError() << '\n';
        raw_display_.close();
        SDL_Quit();
        return 1;
    }
    if (!settings_.create("Settings", settings_columns_)) {
        std::cerr << "Settings window creation failed: " << SDL_GetError() << '\n';
        processed_display_.close();
        raw_display_.close();
        SDL_Quit();
        return 1;
    }

    on_setup();
    if (camera_.is_open() && std::filesystem::exists(config::render::camera_settings_path) &&
        !camera_.load_settings(config::render::camera_settings_path)) {
        std::cerr << "Could not load camera settings: " << camera_.last_error() << '\n';
    }
    settings_.add_camera_controls(settings_columns_ - 1U);
    
    //loop
    std::uint64_t last_render = 0;
    std::uint64_t last_proc_call = 0;
    //tempo timestamp + timeoffset = us()
    std::int64_t time_offset = 0;



    while (raw_display_.is_open() && processed_display_.is_open() && settings_.is_open()) {
        
        uint64_t now = timing::us(); 
        if( now - last_proc_call > config::inputdata::processing_interval ) {
            if ( now - last_proc_call > 2 * config::inputdata::processing_interval)
                std::cout << "proc: can't keep up! took " << now - last_proc_call << std::endl;
            
            last_proc_call = timing::us();
            //updates pseudoframe
            event_source_.read_events(events_);

            //TODO: da fare meglio
            if(time_offset == 0 && events_.size() > 0 )
                time_offset = timing::us() - events_.end()->timestamp_us;

            //oldest event first
            for( unsigned i = 0; i < events_.size(); i++){
                event_queue.append(events_[i]);
                raw_frame[events_[i].y * width + events_[i].x] += (int8_t)events_[i].polarity * 2 - 1;
            }
            
            //removes old pixels
            while( !event_queue.empty() && now - ( time_offset + event_queue.peek().timestamp_us) > config::inputdata::pseudoframe_length){
                if(event_queue.count() == 1){
                    if ((int64_t)now - event_queue.peek().timestamp_us - time_offset < 0){
                        time_offset = timing::us() - event_queue.peek().timestamp_us;
                        std::cout << "updated offset" << std::endl;
                    }
                }
                EventCamera::Event evt = event_queue.pop();
                raw_frame[evt.y * width + evt.x] -= (int8_t)evt.polarity * 2 - 1;
            }

            std::cout <<"\r" << event_queue.count() /1000 << "     " << std::flush;

            process_frame();
        }

        
        if( timing::us() - last_render > config::render::draw_interval ){
            if(timing::us() - last_render > 2 * config::render::draw_interval)
                std::cout << "render: can't keep up!" << std::endl;
            last_render = timing::us();
            //checks window events
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                raw_display_.handle_event(event);
                processed_display_.handle_event(event);
                settings_.handle_event(event);
            }

            settings_.draw();

            //render code
            if (raw_display_.is_playing()) {
                //draw raw
                raw_display_.draw(raw_frame.data());
                
                
                //draw proc
                if (settings_.processing_enabled() && processed_display_.is_playing()) {
                    //ask for a processed frame render
                    
                    processed_display_.draw(frame.data());
                }
            }
        }
    }

    if (camera_.is_open() && !camera_.save_settings(config::render::camera_settings_path)) {
        std::cerr << "Could not save camera settings: " << camera_.last_error() << '\n';
    }
    event_source_.stop();
    settings_.close();
    processed_display_.close();
    raw_display_.close();
    SDL_Quit();
    return 0;
}
