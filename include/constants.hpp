#pragma once
#include <string>

#define RENDER 1

//render stuff
namespace config::render {
    constexpr uint64_t draw_interval = 100'000;

    inline const std::string camera_settings_path = "data/camera_settings.json";
}

namespace config::inputdata {
    //the next two parameters determine the size of the circular buffer for the pseudo frame
    //it can become quite big, size [MB] = max_event_rate[M/s] * pseudoframe_length[s] * 20
    //max event rate of full fov camera can be in the millions
    constexpr int max_event_rate = 200'000;             // events/s
    constexpr uint64_t pseudoframe_length = 3'000'000;  // us
    constexpr uint64_t processing_interval = 3'000;     // us
}