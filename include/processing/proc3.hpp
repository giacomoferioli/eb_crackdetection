#pragma once
#include "processing/processing.hpp"

#include <fstream>
#include <vector>

class Proc3 : public Processing{
    public:
    Proc3(EventSource& source, Config cfg);
    
    protected:

    void on_setup() override;
    void on_termination() override;
    void process_frame(unsigned framenum) override;

    private:

    //std::ofstream f_displacement;

    float& pix(int x,int y) { return frame[y * width + x];}
    float& inter_pix(int x,int y) { return inter_frame[y * width + x];}

    inline float hlinescore(int x,int y,int l) const;

    std::vector<float> frame;
    std::vector<float> inter_frame;
};