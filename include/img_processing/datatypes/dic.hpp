#pragma once

// A rectangular digital-image-correlation region. The reference position
// remains fixed until explicitly updated; x and y track its current position.
struct DicBlock {
    int x0;
    int y0;
    int w;
    int h;
    int x;
    int y;
    // [R] Disabled blocks keep their vector slot so body and flap indices remain aligned.
    bool active = true;

    DicBlock(int x0, int y0, int w, int h)
        : x0(x0), y0(y0), w(w), h(h), x(x0), y(y0) {}

    DicBlock(int x0, int y0, int w, int h, int x, int y)
        : x0(x0), y0(y0), w(w), h(h), x(x), y(y) {}

    int ref_bottom() const { return y0 + h; }
    int bottom() const { return y + h; }

    int center_x() const { return x + w / 2; }
    int ref_center_x() const { return x0 + w / 2; }
    int center_y() const { return y + h / 2; }
    int ref_center_y() const { return y0 + h / 2; }
};

struct DicFlapPair {
    DicBlock top;
    DicBlock bottom;
};
