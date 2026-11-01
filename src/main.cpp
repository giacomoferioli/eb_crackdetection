#include "processing/processing.hpp"
#include "processors/autodistancev2.hpp"
//#include "processors/proc2c.hpp"
//#include "processors/proc2b.hpp"
//#include "processors/autodistance.hpp"
//#include "videosource/event_camera.hpp"
#include "videosource/eblite_playback.hpp"
//#include "processors/specimenfinder.hpp"
#include "videosource/playback.hpp"

#include <memory>

#define LIVE false

int main(int, char**) {
    Config config = load_config("config/config.cfg");

    #if LIVE
    /*  #########################
        #         LIVE          #
        #########################*/
    EventCamera camera(config.camera_config_path);
    if(!camera.is_running()){
        std::cerr << "could not start event camera: " << camera.last_error() << '\n';
        return 1;
    }
    Processing application(camera, config);

    //application.add_processor(std::make_unique<SpecimenFinder>(config));
    //application.add_processor(std::make_unique<Proc2c>(config));
    return application.run_console(camera);
    #else
    /*  #########################
        #       PLAYBACK       #
        #########################*/
    //Playback playback("recordings/fatigue/fatigue2.raw",200,0,500,200);
    EblitePlayback playback("recordings/fatigue/fatigue2.ebl");

    Processing application(playback,config);
    //application.add_processor(std::make_unique<SpecimenFinder>(config));
    application.add_processor(std::make_unique<DummyProcessor>(config));
    return application.run();
    #endif

}
