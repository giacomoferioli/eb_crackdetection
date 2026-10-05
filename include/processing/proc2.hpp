#pragma once
#include "processing/processing.hpp"
#include <cstdint>

#include <fstream>
#include <vector>

class Proc2 : public Processing{
    public:
    Proc2(EventSource& source, Config cfg);
    
    protected:

    void on_setup() override;
    void on_termination() override;
    void process_frame(unsigned framenum) override;

    private:

    std::ofstream f_displacement;

    double correlate(int x, int y, int width, int height, int x0,int y0);
    double correlate(double x, double y, int width, int height, int x0,int y0);

    float& pix(int x,int y) { return frame[y * width + x];}
    std::int16_t& refPS(int x, int y) {return ref_raw_frame[y * width + x];}

    std::vector<std::int16_t> ref_raw_frame;
    std::vector<float> frame;
};