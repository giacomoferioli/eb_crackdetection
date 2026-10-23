#pragma once

#include "processing/processing.hpp"

#include <fstream>

class AutoDistance : public FrameProcessor {
    public:
    explicit AutoDistance(Config cfg);
    
    protected:

    void on_setup(unsigned framenum) override;
    void on_termination(unsigned framenum) override;
    ProcessResult process_frame(unsigned framenum) override;

    private:

    float line_score(int x,int y);
    float score(int y, float dist);

    std::ofstream output_file;
};
