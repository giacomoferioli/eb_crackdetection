#pragma once

#include <algorithm>
#include <cmath>

struct Point {
    float x, y;
};

struct LinePoint : public Point {
    static constexpr float default_maximum_slope = 0.2F;

    float maxm = default_maximum_slope;

    bool operator==(const LinePoint& rhs) const {
        if(std::abs(rhs.x - x) < 1e-3F && std::abs(rhs.y - y) < 1e-3F)
            return true;
        return std::abs((rhs.y - y) / (rhs.x - x)) < std::min(maxm, rhs.maxm);
    }
};

struct Line {
    Point p;
    float m;

    enum Type {
        WHITE,
        BLACK,
        ANY,
        INVALID
    } type = Line::Type::ANY;
    float strength = 1;

    float eval(float x) const { return p.y + (x - p.x) * m; }

    explicit operator bool() const noexcept { return type != INVALID; }

    friend bool operator>(const Line& lhs, const Line& rhs) {
        return lhs.strength > rhs.strength;
    }
    friend bool operator<(const Line& lhs, const Line& rhs) {
        return lhs.strength < rhs.strength;
    }
};
