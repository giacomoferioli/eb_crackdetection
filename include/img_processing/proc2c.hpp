#pragma once

#include "processing/processing.hpp"

#include <vector>

namespace Proc2ccfg {
    // [R] Candidate blocks start on the known opened portion and continue toward
    // the unknown specimen end; endpoint classification will be added later.
    constexpr int block_width = 25;
    constexpr int block_spacing = 25;
    constexpr int right_margin = 10;
}

// [R] Keep Proc2c's DIC type separate from the global Block types currently
// declared by Proc2 and Proc2b, which cannot safely be included together.
struct Proc2cBlock {
    int x0;
    int y0;
    int w;
    int h;
    int x;
    int y;

    Proc2cBlock(int x0, int y0, int w, int h)
        : x0(x0), y0(y0), w(w), h(h), x(x0), y(y0) {}
};

struct Proc2cFlapPair {
    Proc2cBlock top;
    Proc2cBlock bottom;
};

class Proc2c final : public FrameProcessor {
public:
    explicit Proc2c(Config cfg);

protected:
    void on_setup(unsigned framenum) override;
    void on_termination(unsigned framenum) override;
    ProcessResult process_frame(unsigned framenum) override;

private:
    bool DIC_init(float t);
    void procFrame();
    float correlate(int x, int y, const Proc2cBlock& block) const;

    bool initialized_ = false;
    float reference_time_ = 0.0F;
    Frame reference_frame_;
    Frame processed_frame_;
    Frame overlay_frame_;
    std::vector<Proc2cBlock> body_blocks_;
    std::vector<Proc2cFlapPair> flap_blocks_;
};
