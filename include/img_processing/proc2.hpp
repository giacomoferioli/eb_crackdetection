#pragma once
#include "processing/processing.hpp"

#include <fstream>
#include <vector>

namespace Proc2cfg {
    //parameters needed for block creation
    constexpr int blocks_dist_from_border = 10;
    constexpr int blocks_x0 = 560,blocks_y0 = 170;
    constexpr int blocks_w = 50, blocks_h = 50;
    constexpr int blocks_spacing_x = 50,blocks_spacing_y = 10;

    constexpr int blocks_n_x = 18;
    constexpr int blocks_n_y = 1;

    //parameters for autocentering the blocks to the specimen using the specimen borders
    constexpr int specimen_outline_search_interval = 12;
    constexpr int specimen_autocenter_min_blocks = 4;
    constexpr int max_displacement_before_DIC_ref_update = 7;
    //constexpr int specimen_outline_line_thickness = 3; HARDCODED at 4

    //maximum sink rate of the specimen used to define DIC search interval
    constexpr float assumed_max_sinkrate = 0.1;
    constexpr float specimen_body_search_interval = 1.0;
}

struct Block{
    int x0,y0,w,h;
    int x,y;

    Block(int x0, int y0, int w, int h) : x0(x0), y0(y0), w(w), h(h), x(x0), y(y0) {};
    Block(int x0, int y0, int w, int h, int x,int y) : x0(x0), y0(y0), w(w), h(h), x(x), y(y) {};
};

class Proc2 : public FrameProcessor{
    public:
    explicit Proc2(Config cfg);
    
    protected:

    void on_setup() override;
    void on_termination() override;
    ProcessResult process_frame(unsigned framenum) override;

    private:

    float update_blocks_position(int maxoffset);
    float update_DIC_refereces(float blocks_y0);
    float find_specimen_body(float t);
    float find_crack();


    float correlate(int x, int y, Block block);
    float correlate(float x, float y, Block block);

    void init(float t);
    void procFrame();

    float& pix(int x,int y) { return frame[y * width + x];}
    float& refPS(int x, int y) {return ref_raw_frame[y * width + x];}
    float& procPS(int x, int y) {return procframe[y * width + x];}

    /*  useful data for crack search */
       double midline; 

    /* */
    std::vector<Block> blocks;

    float ref_frame_last_save;
    std::vector<float> ref_raw_frame;

    std::vector<float> procframe;
    std::vector<float> frame;

    std::ofstream output_file;
};
