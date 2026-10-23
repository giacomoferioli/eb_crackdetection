#pragma once

#include "img_processing/datatypes/geometry.hpp"

#include <algorithm>
#include <cmath>

// [R] Search columns start at search_start_x; line search is limited to the given y interval.
struct SpecimenSearchParameters {
    int number_of_search_columns;
    int search_column_width;
    int search_start_x;
    int line_search_start_y;
    int line_search_end_y;
    float maximum_tilt_degrees;
    float expected_thickness;
    unsigned minimum_agreeing_guesses;
    unsigned maximum_disagreeing_guesses;
};

// Shared specimen geometry produced by one processor and consumable by later
// processors. Equality means that two independent detections describe the same
// slightly slanted specimen; crack position is intentionally not part of it.
struct SpecimenPositionInfo {
    static constexpr float maximum_slope_difference = 0.02F;
    static constexpr float maximum_top_distance = 2.0F;
    static constexpr float maximum_relative_thickness_difference = 0.05F;

    Line top{{0.0F, 0.0F}, 0.0F, Line::INVALID, 0.0F};
    float thickness = 0.0F;
    float crackpos = 0.0F;

    bool operator==(const SpecimenPositionInfo& rhs) const {
        if(!top || !rhs.top || top.type != rhs.top.type)
            return false;

        const float refx = 0.5F * (top.p.x + rhs.top.p.x);
        const float thickness_scale = std::max(std::abs(thickness), std::abs(rhs.thickness));
        const bool thickness_matches = thickness_scale < 1e-6F
            ? std::abs(thickness - rhs.thickness) < 1e-6F
            : std::abs(thickness - rhs.thickness) / thickness_scale <
                maximum_relative_thickness_difference;

        return std::abs(top.m - rhs.top.m) < maximum_slope_difference
            && std::abs(top.eval(refx) - rhs.top.eval(refx)) <
                maximum_top_distance +
                std::abs(top.p.x - rhs.top.p.x) * maximum_slope_difference
            && thickness_matches;
    }
};
