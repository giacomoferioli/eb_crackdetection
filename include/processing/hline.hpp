#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

struct HLinePoint {
    int x;
    float y;
};

// A discrete line y = f(x), defined for every integer x in [a, b].
class HLine {
public:
    HLine(int start_x, std::vector<float> points, double score = 0.0);

    int a() const noexcept { return start_x_; }
    int b() const noexcept;
    int start_x() const noexcept { return a(); }
    int end_x() const noexcept { return b(); }

    HLinePoint start() const noexcept;
    HLinePoint end() const noexcept;

    std::size_t length() const noexcept { return points_.size(); }
    bool contains(int x) const noexcept { return x >= a() && x <= b(); }

    float y_at(int x) const;
    float operator[](std::size_t offset) const { return points_.at(offset); }
    const std::vector<float>& points() const noexcept { return points_; }

    // Mean signed line-detector response along the line. Higher is better.
    double score() const noexcept { return score_; }

private:
    int start_x_;
    std::vector<float> points_;
    double score_;
};

// Lines are the same only when they overlap in x and their vertical distance
// never exceeds maximum_difference anywhere in that overlap.
bool hlines_are_same(const HLine& first,
                     const HLine& second,
                     float maximum_difference);

// Builds a line over the union of both x domains. In the overlap, samples from
// the longer (then higher-scoring) detection are retained.
HLine merge_hlines(const HLine& first, const HLine& second);

// Inserts a distinct line or merges it with every matching line already stored.
void insert_or_merge_hline(std::vector<HLine>& lines,HLine candidate,float maximum_difference);

enum class HLinePolarity {
    Positive,
    Negative,
    Either
};

enum class HLineRanking {
    Length,
    Score
};

struct HLineFinderOptions {
    // Every returned line crosses this column.
    int seed_x = 0;

    // Seed search interval [min_y, max_y). A negative max_y means image height.
    int min_y = 0;
    int max_y = -1;

    HLinePolarity polarity = HLinePolarity::Either;
    HLineRanking ranking = HLineRanking::Length;

    int thickness = 1;
    int scoring_half_width = 3;
    int minimum_length = 20;
    int max_vertical_step = 1;
    // Number of initial scores, including the seed, averaged into a fixed reference.
    int reference_sample_count = 5;

    // Negative selects the Proc1-style default: 2 * half-width * thickness.
    double minimum_seed_score = -1.0;
    double continuation_ratio = 0.5;
    double inertia = 0.7;

    // Candidates closer than this are treated as detections of the same line.
    // A negative value uses max(1, thickness).
    float duplicate_distance = -1.0F;
};

// Finds up to maximum_lines largest/strongest distinct lines crossing seed_x.
// Pixels are a row-major width-by-height signed pseudoframe.
std::vector<HLine> find_hlines(const std::int16_t* pixels,
                               int width,
                               int height,
                               const HLineFinderOptions& options,
                               std::size_t maximum_lines);

std::vector<HLine> find_hlines(const std::vector<std::int16_t>& pixels,
                               int width,
                               int height,
                               const HLineFinderOptions& options,
                               std::size_t maximum_lines);

// Convenience wrapper returning the largest/strongest detected line.
std::optional<HLine> find_hline(const std::int16_t* pixels,
                                int width,
                                int height,
                                const HLineFinderOptions& options);

std::optional<HLine> find_hline(const std::vector<std::int16_t>& pixels,
                                int width,
                                int height,
                                const HLineFinderOptions& options);
