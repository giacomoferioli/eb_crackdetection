#pragma once
#include "processing/processing.hpp"

#include <fstream>

class Proc1 : public Processing{
    public:
    Proc1(EventSource& source, Config cfg);
    
    protected:

    void on_setup() override;
    void on_termination() override;
    void process_frame(unsigned framenum) override;

    private:

    std::ofstream f_displacement;
    int rightmost_x;

    float& pix(int x,int y) { return frame[y * width + x];}
    int hlinefinder(int x,int y0,int y1,bool type, int h = 1, int mininum_length = 20, bool first = false);
    inline int score(int x,int y,int l) const;

    std::vector<float> frame;
};