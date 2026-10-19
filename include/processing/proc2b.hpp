#pragma once
#include "frame.hpp"
#include "processing/processing.hpp"
#include <cstdint>

#include <fstream>
#include <vector>

namespace Proc2cfg {
    //parameters needed for block creation
    constexpr int blocks_dist_from_border = 10;
    constexpr int blocks_x0 = 560,blocks_y0 = 160;
    constexpr int blocks_w = 25, blocks_h = 25;
    constexpr int blocks_spacing_x = 25,blocks_spacing_y = 33;

    constexpr int blocks_n_x = 44;
    constexpr int blocks_n_y = 2;

    //parameters for autocentering the blocks to the specimen using the specimen borders
    constexpr int specimen_outline_search_interval = 15;
    constexpr int specimen_autocenter_min_blocks = 4;
    constexpr int max_displacement_before_DIC_ref_update = 5;
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

    int ref_bottom() {return y0 + h;}
    int bottom() {return y + h;}

    int center_x() { return x + w/2;}
    int ref_center_x() { return x0 + w/2;}
    int center_y() { return y + h/2;}
    int ref_center_y() { return y0 + h/2;}
};

class Proc2b : public Processing{
    public:
    Proc2b(EventSource& source, Config cfg);
    
    protected:

    void on_setup() override;
    void on_termination() override;
    void process_frame(unsigned framenum) override;

    private:

    float update_blocks_position(int maxoffset);
    float update_DIC_refereces(float blocks_y0);
    float find_specimen_body(float t);
    float find_crack();


    float correlate(int x, int y, Block block);
    float correlate(float x, float y, Block block);

    void init(float t);
    void procFrame();

    Block& top_block(size_t i) { return blocks[2 * i]; }
    Block& btm_block(size_t i) { return blocks[2 * i + 1]; }

    /*  useful data for crack search */
    double midline; 
    int uncracked_specimen = 10;

    /* */
    std::vector<Block> blocks;

    float ref_frame_last_save;
    Frame reference_frame;
    Frame processed_frame;
    Frame overlay_frame;

    std::ofstream output_file;
};
