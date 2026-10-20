#include "img_processing/proc2b.hpp"
//#include "videosource/event_camera.hpp"
#include "processing/processing.hpp"
#include "videosource/playback.hpp"

#include <memory>


int main(int, char**) {
    Config config = load_config("config/config.cfg");
    Playback playback("data/test2.raw", config.realtime);
    Processing application(playback, config);
    application.add_processor(std::make_unique<Proc2b>(config));
    return application.run();
}
