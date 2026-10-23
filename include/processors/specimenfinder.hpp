#pragma once

#include "processing/processing.hpp"

#include <optional>

#define SF_DRAW true

namespace SFcfg {
    constexpr int number_of_search_column = 8;
    constexpr int search_column_width = 20;

    constexpr float max_speciment_tilt = 5;
    constexpr float specimen_thickness = 5.2;

    //let's call these two guys a and b
    //to say we have found the specimen we need at least a equal reading against maximum b outliers
    constexpr int mininum_number_of_agreeing_guesses = 4;
    constexpr int maximum_number_of_disagreeing_guesses = 2;

    //
    constexpr int minimum_reading_toexit = 3;
    constexpr int maximum_disagreeing_readings = 2;
}

class SpecimenFinder : public FrameProcessor {
    public:
    explicit SpecimenFinder(Config cfg);
    
    protected:

    void on_setup(unsigned framenum) override;
    void on_termination(unsigned framenum) override;
    ProcessResult process_frame(unsigned framenum) override;

    std::optional<SpecimenPositionInfo> result() { return specimen_pos; }

    private:

    void procFrame();

    std::optional<SpecimenPositionInfo> specimen_pos = std::nullopt;
    float ref_frame_last_save;
    Frame processed_frame;
    
    #if SF_DRAW
    Frame overlay_frame;
    #endif
};
