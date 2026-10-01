#include "processing/proc1.hpp"
//#include "videosource/event_camera.hpp"
#include "videosource/playback.hpp"


int main(int, char**) {
    Config config = load_config("data/config.json");
    Playback playback("data/test-2mm-15offbs.raw");
    Proc1 application(playback, config);
    return application.run();
    
}
