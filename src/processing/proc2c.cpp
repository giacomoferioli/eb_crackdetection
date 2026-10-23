#include "img_processing/proc2c.hpp"

#include "image_saver.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {
constexpr float overlay_scaling = 1.0F;
constexpr float raw_scaling = 16.0F;
}

Proc2c::Proc2c(Config cfg) : FrameProcessor(cfg) {}

void Proc2c::on_setup(unsigned framenum) {
    processed_frame_.reset(width, height, 0.0F);
    reference_frame_.reset(width, height, 0.0F);
    overlay_frame_.reset(width, height, 0.0F);

    // [R] Proc2c owns the displayed frames, but Processing still controls when
    // the resulting image is saved.
    img_saver = std::make_unique<ImageSaver>(
        [this](int ix) {
            const int gray = std::clamp(
                static_cast<int>(std::lround(processed_frame_[ix] * raw_scaling + 128.0F)),
                0,
                255);
            const float alpha = std::clamp(overlay_frame_[ix] * overlay_scaling, 0.0F, 1.0F);
            const unsigned red = static_cast<unsigned>(std::lround(alpha * 255.0F + (1.0F - alpha) * gray));
            const unsigned green_blue = static_cast<unsigned>(std::lround((1.0F - alpha) * gray));
            return red << 24U | green_blue << 16U | green_blue << 8U | 0xFFU;
        },
        width,
        height,
        "proc_images/");

    // [R] Initialization uses the first frame assigned to Proc2c as its DIC
    // reference and as the time origin for future reference updates.
    initialized_ = DIC_init(framenum * cfg.processing_interval);
}

void Proc2c::on_termination(unsigned) {}

ProcessResult Proc2c::process_frame(unsigned) {
    if(!initialized_)
        return ProcessResult::ERR;

    // [R] Tracking is intentionally deferred; keep the processed output current
    // so the next implementation step can operate on the existing skeleton.
    overlay_frame_.fill(0.0F);
    procFrame();
    return ProcessResult::OK;
}

bool Proc2c::DIC_init(float t) {
    body_blocks_.clear();
    flap_blocks_.clear();

    const std::optional<SpecimenPositionInfo>& specimen = shared_data().specimen_position;
    if(!specimen || !specimen->top || specimen->thickness <= 0.0F){
        std::cerr << "Proc2c cannot initialize without valid specimen position data.\n";
        return false;
    }

    procFrame();
    reference_frame_ = processed_frame_;
    reference_time_ = t;

    const int body_height = std::max(2, static_cast<int>(std::lround(specimen->thickness)));
    const int top_height = body_height / 2;
    const int bottom_height = body_height - top_height;
    const int first_x = width / 4;
    const int last_x = width - Proc2ccfg::right_margin - Proc2ccfg::block_width;

    // [R] These are deliberately candidate blocks: some rightmost blocks may be
    // past the unknown specimen end and will be rejected by later tracking logic.
    for(int x = first_x; x <= last_x; x += Proc2ccfg::block_spacing){
        const float center_x = x + 0.5F * Proc2ccfg::block_width;
        const int top_y = static_cast<int>(std::lround(specimen->top.eval(center_x)));
        if(top_y < 0 || top_y + body_height > height)
            continue;

        body_blocks_.emplace_back(x, top_y, Proc2ccfg::block_width, body_height);
        flap_blocks_.push_back({
            Proc2cBlock(x, top_y, Proc2ccfg::block_width, top_height),
            Proc2cBlock(x, top_y + top_height, Proc2ccfg::block_width, bottom_height)
        });
    }

    if(body_blocks_.empty()){
        std::cerr << "Proc2c could not place any candidate blocks inside the frame.\n";
        return false;
    }

    return true;
}

void Proc2c::procFrame() {
    processed_frame_.fill(0.0F);
    BasicFrame<std::int16_t> raw_frame = get_raw_frame();

    // [R] Suppress isolated hot pixels before applying the same compact blur
    // used by the existing Proc2 processors.
    for(int y = 1; y < height - 1; y++){
        for(int x = 1; x < width - 1; x++){
            const float neighbour_average = (
                std::abs(raw_frame(x - 1, y)) + std::abs(raw_frame(x + 1, y)) +
                std::abs(raw_frame(x, y - 1)) + std::abs(raw_frame(x, y + 1))) / 4.0F;

            if(std::abs(raw_frame(x, y)) > 5 &&
               std::abs(raw_frame(x, y)) > 8.0F * neighbour_average){
                raw_frame(x, y) = raw_frame(x, y) > 0
                    ? static_cast<std::int16_t>(std::lround(neighbour_average))
                    : static_cast<std::int16_t>(-std::lround(neighbour_average));
            }
        }
    }

    for(int y = 1; y < height - 1; y++){
        for(int x = 1; x < width - 1; x++){
            processed_frame_(x, y) = (
                12.0F * raw_frame(x, y) +
                2.0F * (raw_frame(x + 1, y) + raw_frame(x - 1, y) +
                        raw_frame(x, y + 1) + raw_frame(x, y - 1)) +
                raw_frame(x + 1, y + 1) + raw_frame(x - 1, y + 1) +
                raw_frame(x - 1, y - 1) + raw_frame(x + 1, y - 1)) / 24.0F;
        }
    }
}

float Proc2c::correlate(int x, int y, const Proc2cBlock& block) const {
    // [R] Reject both negative and upper-bound violations before indexing either
    // the current image or the stored DIC reference patch.
    if(x < 0 || y < 0 || block.x0 < 0 || block.y0 < 0 ||
       x + block.w > width || y + block.h > height ||
       block.x0 + block.w > width || block.y0 + block.h > height){
        std::cerr << "[Proc2c/correlate] invalid current position " << x << ' ' << y
                  << ", reference block: " << block.x0 << ' ' << block.y0 << ' '
                  << block.w << ' ' << block.h << '\n';
        return std::numeric_limits<float>::infinity();
    }

    float squared_error = 0.0F;
    for(int dx = 0; dx < block.w; dx++){
        for(int dy = 0; dy < block.h; dy++){
            const float difference = processed_frame_(x + dx, y + dy) -
                                     reference_frame_(block.x0 + dx, block.y0 + dy);
            squared_error += difference * difference;
        }
    }
    return squared_error;
}
