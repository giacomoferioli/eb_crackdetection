#pragma once

#include "img_processing/datatypes/dic.hpp"
#include "processing/processing.hpp"

#include <fstream>
#include <vector>

/* Test 1
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
*/
namespace Proc2cfg {
    //parameters needed for block creation
    constexpr int blocks_dist_from_border = 10;
    constexpr int blocks_x1 = 1220,blocks_y0 = 80;
    constexpr int blocks_w = 20, blocks_h = 17;
    constexpr int blocks_spacing_x = 20,blocks_spacing_y = 17;

    constexpr int blocks_n_x = 40;
    constexpr int blocks_n_y = 2;

    //parameters for autocentering the blocks to the specimen using the specimen borders
    constexpr int specimen_outline_search_interval = 12;
    constexpr int specimen_autocenter_min_blocks = 4;
    constexpr int max_displacement_before_DIC_ref_update = 5;
    //constexpr int specimen_outline_line_thickness = 3; HARDCODED at 4

    //maximum sink rate of the specimen used to define DIC search interval
    constexpr float assumed_max_sinkrate = 0.1;
    constexpr float specimen_body_search_interval = 1.0;
}

class Proc2b : public FrameProcessor{
    public:
    explicit Proc2b(Config cfg);
    
    protected:

    void on_setup(unsigned framenum) override;
    void on_termination(unsigned framenum) override;
    ProcessResult process_frame(unsigned framenum) override;

    private:

    float update_blocks_position(int maxoffset);
    float update_DIC_refereces(float blocks_y0);
    float find_specimen_body(float t);
    float find_crack();


    float correlate(int x, int y, DicBlock block);
    float correlate(float x, float y, DicBlock block);

    void init(float t);
    void procFrame();

    DicBlock& top_block(size_t i) { return blocks[2 * i]; }
    DicBlock& btm_block(size_t i) { return blocks[2 * i + 1]; }

    /*  useful data for crack search */
    double midline; 
    int uncracked_specimen = 10;

    /* */
    std::vector<DicBlock> blocks;

    float ref_frame_last_save;
    Frame reference_frame;
    Frame processed_frame;
    Frame overlay_frame;

    std::ofstream output_file;
    std::ofstream output_file_2;
};
