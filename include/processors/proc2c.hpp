#pragma once

#include "img_processing/datatypes/dic.hpp"
#include "img_processing/datatypes/specimen.hpp"
#include "processing/processing.hpp"

#include <fstream>
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
    constexpr int crack_fit_block_count = 3;
    constexpr float max_zero_distance = 2.0F;
    constexpr size_t maximum_thickness_measurements = 8;
    constexpr size_t minimum_thickness_measurements = 4;
    constexpr size_t pixel_scale_median_window = 5;
    constexpr float pixel_scale_smoothing_factor = 0.2F;
    constexpr float thickness_outlier_sigma = 3.5F;
    constexpr float minimum_thickness_outlier_distance = 1.0F;
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
    void publish_tracking_regions();
    bool update_pixel_scale(unsigned framenum);
    float get_start_position() const;
    float horizontal_pixels_per_mm() const;
    float vertical_pixels_per_mm() const;
    float dy_since_ref(unsigned framenum) const;
    float find_crack(float& interpolation_center_x_mm);

    bool initialized = false;

    struct {
        unsigned framenum = 0u;
        Frame reference_frame;
        SpecimenPositionInfo specimen_pos;
    } dic_reference;

    bool right_edge_checked = false;
    Frame processed_frame;
    Frame overlay_frame;
    std::vector<DicBlock> body_blocks;
    std::vector<DicFlapPair> flap_blocks;
    struct ThicknessMeasurement {
        int x = 0;
        float top_y = 0.0F;
        float bottom_y = 0.0F;
        bool accepted = false;
    };
    std::vector<ThicknessMeasurement> thickness_measurements;
    std::vector<float> recent_vertical_pixel_scales;
    float raw_vertical_pixels_per_mm = 0.0F;
    float filtered_vertical_pixels_per_mm = 0.0F;
    float current_horizontal_pixels_per_mm = 0.0F;
    std::ofstream output_file;
    std::ofstream pixel_scale_file;
};
