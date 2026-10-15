#pragma once

#include "processing/hline.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// HLine4 finder tuning. These are intentionally defines so the complete
// tracking behaviour can be adjusted from one place.
// Degree of the polynomial fitted once to each initial HLine.
#define HLINE4_POLYNOMIAL_DEGREE 4
// Number of coefficients, including the constant coefficient.
#define HLINE4_COEFFICIENT_COUNT (HLINE4_POLYNOMIAL_DEGREE + 1)
// Maximum |d²y/dx²|, in vertical pixels per horizontal pixel squared.
#define HLINE4_MAX_CURVATURE 1e-3
// Below this value the fourth-degree term is treated as zero when enforcing
// a constant curvature sign.
#define HLINE4_MINIMUM_QUARTIC_COEFFICIENT 1e-10
// Horizontal radius of the scoring window; its total width is 2*N+1 pixels.
#define HLINE4_SCORING_HALF_WIDTH 3
// Vertical height, in pixels, of the line-detector scoring window.
#define HLINE4_LINE_THICKNESS 2
// Largest vertical movement allowed per x step by the initial HLine finder.
#define HLINE4_INITIAL_MAX_VERTICAL_STEP 1
// Vertical radius searched around the fixed polynomial during extension.
#define HLINE4_EXTENSION_SEARCH_RADIUS 2
// Consecutive missing columns allowed before polynomial extension stops.
#define HLINE4_MAX_EXTENSION_GAP 4
// Minimum number of samples required from the initial HLine finder.
#define HLINE4_MINIMUM_LENGTH 50
// Minimum signed sum required to accept a starting point for a line.
#define HLINE4_MINIMUM_SEED_SCORE (2.0 * HLINE4_SCORING_HALF_WIDTH * HLINE4_LINE_THICKNESS)
// A continuation point must score above this fraction of the reference score.
#define HLINE4_CONTINUATION_RATIO 0.6
// Number of initial scores averaged once to create the fixed termination reference.
#define HLINE4_REFERENCE_SAMPLE_COUNT 5
// Weight given to the previous/predicted y; lower values follow measurements more closely.
#define HLINE4_POSITION_INERTIA 0.7
// Maximum mean vertical separation for treating two detections as one line.
#define HLINE4_DUPLICATE_DISTANCE 2.0F

struct HLine4Polynomial {
    std::array<double,HLINE4_COEFFICIENT_COUNT> coefficients{};
    double x_centre = 0;
    double x_scale = 1;
};

// A sampled horizontal line together with its fourth-degree approximation.
class HLine4 : public HLine {
public:
    HLine4(int start_x,std::vector<float> points,double score = 0.0);
    HLine4(int start_x,std::vector<float> points,HLine4Polynomial polynomial,double score);

    double polynomial_y(double x) const;
    const HLine4Polynomial& polynomial() const noexcept {
        return polynomial_;
    }

private:
    HLine4Polynomial polynomial_;
};

bool hlines4_are_same(const HLine4& first,const HLine4& second);
HLine4 merge_hlines4(const HLine4& first,const HLine4& second);
void insert_or_merge_hline4(std::vector<HLine4>& lines,HLine4 candidate);

// First runs the original HLine finder, then fits one quartic to each result and
// uses that fixed fit to search for more points beyond both line endpoints.
std::vector<HLine4> find_hlines4(const std::int16_t* pixels,
                                 int width,
                                 int height,
                                 int seed_x,
                                 HLinePolarity polarity,
                                 std::size_t maximum_lines);
