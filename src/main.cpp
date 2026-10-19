#include "processing/proc2b.hpp"
//#include "videosource/event_camera.hpp"
#include "processing/processing.hpp"
#include "videosource/playback.hpp"


int main(int, char**) {
    Config config = load_config("data/config.json");
    Playback playback("data/long.raw", config.realtime);
    Proc2b application(playback, config);
    return application.run();
}
