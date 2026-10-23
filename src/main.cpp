#include "processors/proc2c.hpp"
//#include "processors/proc2b.hpp"
//#include "processors/autodistance.hpp"
#include "videosource/event_camera.hpp"
#include "processors/specimenfinder.hpp"
#include "processing/processing.hpp"
#include "videosource/playback.hpp"

#include <iostream>
#include <memory>

#define LIVE false

int main(int, char**) {
    Config config = load_config("config/config.cfg");

    #if LIVE
    // [R] Live mode opens the configured camera but leaves every console feature off.
    EventCamera camera(config.camera_config_path);
    if(!camera.is_running()){
        std::cerr << "could not start event camera: " << camera.last_error() << '\n';
        return 1;
    }
    Processing application(camera, config);

    // [R] The registered processor is dormant until command 30, 32, or 40 starts it.
    application.add_processor(std::make_unique<SpecimenFinder>(config));
    return application.run_console(camera);
    #else
    Playback playback("recordings/long.raw");
    Processing application(playback,config);
    application.add_processor(std::make_unique<SpecimenFinder>(config));
    application.add_processor(std::make_unique<Proc2c>(config));
    return application.run();
    #endif

}
