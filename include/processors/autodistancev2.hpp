#pragma once

#include "img_processing/frame.hpp"
#include "processing/processing.hpp"

#include <fstream>

#define AD_DRAW 1

class AutoDistancev2 : public FrameProcessor {
    public:
    explicit AutoDistancev2(Config cfg);
    
    protected:

    void on_setup(unsigned framenum) override;
    void on_termination(unsigned framenum) override;
    ProcessResult process_frame(unsigned framenum) override;

    private:

    Frame processed_frame;
    #if AD_DRAW
    Frame overlay_frame;
    #endif
    std::ofstream output_file;
};
