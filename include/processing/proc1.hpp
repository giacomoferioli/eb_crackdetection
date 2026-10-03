#pragma once
#include "processing/processing.hpp"
#include <cstdint>

#include <fstream>

class Proc1 : public Processing{
    public:
    Proc1(EventSource& source, Config cfg);
    
    protected:

    void on_setup() override;
    void on_termination() override;
    void process_frame() override;

    private:

    std::ofstream f_displacement;

    std::int16_t& pix(int x,int y) { return frame[y * width + x];}

    int hlinefinder(int x,bool type, int h = 1, int mininum_length = 20, bool first = false);

    inline int score(int x,int y,int l) const;

    int* matrix;
    std::vector<std::int16_t> frame;
};