#pragma once
#include "processing/processing.hpp"

#include "processing/hline4.hpp"

#include <cstdint>
#include <vector>

class Proc3 : public Processing{
    public:
    Proc3(EventSource& source, Config cfg);
    
    protected:

    void on_setup() override;
    void on_termination() override;
    void process_frame(unsigned framenum) override;

    private:

    std::vector<HLine4> find_lines(HLinePolarity polarity, int n);

    //std::ofstream f_displacement;
    void general_identification();

    std::uint32_t& pix(int x,int y) { return frame[y * width + x];}

    std::vector<std::uint32_t> frame;
};
