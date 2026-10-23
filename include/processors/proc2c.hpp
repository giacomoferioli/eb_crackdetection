#pragma once

#include "img_processing/datatypes/dic.hpp"
#include "img_processing/datatypes/specimen.hpp"
#include "processing/processing.hpp"

#include <vector>

#define P2C_DRAW true

namespace Proc2ccfg {
    constexpr int block_width = 25;
    constexpr int block_spacing = 25;
    constexpr int right_margin = 10;
    constexpr int specimen_outline_search_interval = 12;
    constexpr int max_displacement_before_DIC_ref_update = 5;
    constexpr float max_uncracked_thickness_increase = 0.10F;
    constexpr float max_right_block_displacement_error = 2.0F;
}

class Proc2c final : public FrameProcessor {
public:
    explicit Proc2c(Config cfg);

protected:
    void on_setup(unsigned framenum) override;
    void on_termination(unsigned framenum) override;
    ProcessResult process_frame(unsigned framenum) override;

private:
    bool DIC_init(unsigned framenum);
    void procFrame();
    float correlate(int x, int y, const DicBlock& block) const;

    void draw();

    void update_blocks_position(int maxoffset);
    void disable_invalid_blocks();
    void update_dic_reference(unsigned framenum);

    bool initialized = false;

    struct {
        unsigned framenum = 0u;
        Frame reference_frame;
        SpecimenPositionInfo specimen_pos;
    } dic_reference;
    float dy_since_ref(unsigned framenum) { return (framenum - dic_reference.framenum) * cfg.processing_interval * cfg.displacement_rate * cfg.pixels_per_mm / 60.;  }

    float uncracked_specimen_thickness = 0.0F;
    bool right_edge_checked = false;
    Frame processed_frame;
    Frame overlay_frame;
    std::vector<DicBlock> body_blocks;
    std::vector<DicFlapPair> flap_blocks;
};
