#include "processing/hline.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

HLine::HLine(int start_x, std::vector<float> points, double score)
    : start_x_(start_x), points_(std::move(points)), score_(score) {
    if (points_.empty()) {
        throw std::invalid_argument("an HLine must contain at least one point");
    }
}

int HLine::b() const noexcept {
    return start_x_ + static_cast<int>(points_.size()) - 1;
}

HLinePoint HLine::start() const noexcept {
    return {a(), points_.front()};
}

HLinePoint HLine::end() const noexcept {
    return {b(), points_.back()};
}

float HLine::y_at(int x) const {
    if (!contains(x)) {
        throw std::out_of_range("x is outside the HLine domain");
    }
    return points_[static_cast<std::size_t>(x - start_x_)];
}

bool hlines_are_same(const HLine& first,const HLine& second,float maximum_difference) {
    if (maximum_difference < 0.0F) {
        throw std::invalid_argument("maximum line difference must be non-negative");
    }

    const int overlap_start = std::max(first.a(), second.a());
    const int overlap_end = std::min(first.b(), second.b());
    if (overlap_start > overlap_end) return false;

    int totdistance = 0;
    for (int x = overlap_start; x <= overlap_end; ++x)
        totdistance += std::abs(first.y_at(x) - second.y_at(x));
    return ((double)totdistance / (overlap_end - overlap_start + 1)) < maximum_difference;
}

HLine merge_hlines(const HLine& first, const HLine& second) {
    const HLine* preferred = &first;
    const HLine* other = &second;
    if (second.length() > first.length() ||
        (second.length() == first.length() && second.score() > first.score())) {
        preferred = &second;
        other = &first;
    }

    const int union_start = std::min(first.a(), second.a());
    const int union_end = std::max(first.b(), second.b());
    std::vector<float> points;
    points.reserve(static_cast<std::size_t>(union_end - union_start + 1));

    for (int x = union_start; x <= union_end; ++x) {
        if (preferred->contains(x)) {
            points.push_back(preferred->y_at(x));
        } else {
            points.push_back(other->y_at(x));
        }
    }

    const double combined_score =
        (first.score() * static_cast<double>(first.length()) +
         second.score() * static_cast<double>(second.length())) /
        static_cast<double>(first.length() + second.length());
    return HLine(union_start, std::move(points), combined_score);
}

void insert_or_merge_hline(std::vector<HLine>& lines,
                           HLine candidate,
                           float maximum_difference) {
    // Restart after every merge: the enlarged union may overlap a line that
    // the original candidate did not overlap.
    for (std::size_t index = 0; index < lines.size();) {
        if (!hlines_are_same(candidate, lines[index], maximum_difference)) {
            ++index;
            continue;
        }

        candidate = merge_hlines(candidate, lines[index]);
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(index));
        index = 0;
    }
    lines.push_back(std::move(candidate));
}

namespace {

struct Candidate {
    HLine line;
};

struct Scorer {
    const std::int16_t* pixels;
    int width;
    int height;
    int half_width;
    int upper_half_thickness;
    int lower_half_thickness;
    int polarity;

    bool can_score(int x, int y) const noexcept {
        return x >= half_width && x < width - half_width &&
               y >= upper_half_thickness &&
               y < height - lower_half_thickness;
    }

    double operator()(int x, int y) const noexcept {
        if (!can_score(x, y)) {
            return -std::numeric_limits<double>::infinity();
        }

        std::int64_t sum = 0;
        for (int sample_x = x - half_width;
             sample_x <= x + half_width; ++sample_x) {
            for (int sample_y = y - upper_half_thickness;
                 sample_y <= y + lower_half_thickness; ++sample_y) {
                sum += pixels[static_cast<std::size_t>(sample_y) * width +
                              sample_x];
            }
        }
        return static_cast<double>(polarity) * static_cast<double>(sum);
    }
};

struct TracedSide {
    std::vector<float> points;
    double score_sum = 0.0;
};

TracedSide trace_side(const Scorer& scorer,
                      int seed_x,
                      float seed_y,
                      double reference_score,
                      int direction,
                      const HLineFinderOptions& options) {
    TracedSide result;
    float running_y = seed_y;
    double reference_sum = reference_score;
    int reference_samples = 1;

    for (int x = seed_x + direction;
         x >= scorer.half_width && x < scorer.width - scorer.half_width;
         x += direction) {
        const int centre_y = static_cast<int>(std::lround(running_y));
        int best_y = centre_y;
        double best_score = -std::numeric_limits<double>::infinity();

        for (int delta_y = -options.max_vertical_step;
             delta_y <= options.max_vertical_step; ++delta_y) {
            const int candidate_y = centre_y + delta_y;
            const double candidate_score = scorer(x, candidate_y);
            if (candidate_score > best_score) {
                best_score = candidate_score;
                best_y = candidate_y;
            }
        }

        // The first samples establish one fixed reference. It is never updated
        // again, so a weakening line cannot lower its own termination threshold.
        if (reference_samples >= options.reference_sample_count &&
            best_score <= reference_score * options.continuation_ratio) break;

        running_y = static_cast<float>(
            options.inertia * running_y +
            (1.0 - options.inertia) * static_cast<double>(best_y));
        result.points.push_back(running_y);
        result.score_sum += best_score;
        if (reference_samples < options.reference_sample_count) {
            reference_sum += best_score;
            reference_samples++;
            if (reference_samples == options.reference_sample_count)
                reference_score = reference_sum / reference_samples;
        }
    }

    return result;
}

HLine trace_line(const Scorer& scorer,
                 int seed_x,
                 int seed_y,
                 double seed_score,
                 const HLineFinderOptions& options) {
    TracedSide left = trace_side(scorer, seed_x, static_cast<float>(seed_y),
                                 seed_score, -1, options);
    TracedSide right = trace_side(scorer, seed_x, static_cast<float>(seed_y),
                                  seed_score, 1, options);

    std::vector<float> points;
    points.reserve(left.points.size() + 1U + right.points.size());
    points.insert(points.end(), left.points.rbegin(), left.points.rend());
    points.push_back(static_cast<float>(seed_y));
    points.insert(points.end(), right.points.begin(), right.points.end());

    const double mean_score =
        (left.score_sum + seed_score + right.score_sum) /
        static_cast<double>(points.size());
    const int start_x = seed_x - static_cast<int>(left.points.size());
    return HLine(start_x, std::move(points), mean_score);
}

void validate_options(const std::int16_t* pixels,
                      int width,
                      int height,
                      const HLineFinderOptions& options) {
    if (pixels == nullptr) {
        throw std::invalid_argument("pixels must not be null");
    }
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("image dimensions must be positive");
    }
    if (options.thickness <= 0 || options.scoring_half_width < 0 ||
        options.minimum_length <= 0 || options.max_vertical_step < 0 ||
        options.reference_sample_count <= 0) {
        throw std::invalid_argument("invalid HLine finder dimensions");
    }
    if (options.continuation_ratio < 0.0 ||
        options.continuation_ratio > 1.0) {
        throw std::invalid_argument("continuation_ratio must be in [0, 1]");
    }
    if (options.inertia < 0.0 || options.inertia >= 1.0) {
        throw std::invalid_argument("inertia must be in [0, 1)");
    }
    if (options.seed_x < options.scoring_half_width ||
        options.seed_x >= width - options.scoring_half_width) {
        throw std::invalid_argument("seed_x is too close to an image edge");
    }
}

} // namespace

std::vector<HLine> find_hlines(const std::int16_t* pixels,
                               int width,
                               int height,
                               const HLineFinderOptions& options,
                               std::size_t maximum_lines) {
    validate_options(pixels, width, height, options);
    if (maximum_lines == 0) return {};

    const int upper_half_thickness = (options.thickness - 1) / 2;
    const int lower_half_thickness = options.thickness / 2;
    const int search_start = std::max(options.min_y, upper_half_thickness);
    const int configured_end = options.max_y < 0 ? height : options.max_y;
    const int search_end = std::min(configured_end,
                                    height - lower_half_thickness);
    if (search_start >= search_end) return {};

    const double minimum_seed_score = options.minimum_seed_score >= 0.0
        ? options.minimum_seed_score
        : 2.0 * options.scoring_half_width * options.thickness;

    std::vector<int> polarities;
    if (options.polarity != HLinePolarity::Negative) polarities.push_back(1);
    if (options.polarity != HLinePolarity::Positive) polarities.push_back(-1);

    std::vector<Candidate> candidates;
    for (int polarity : polarities) {
        const Scorer scorer{pixels, width, height,
                            options.scoring_half_width,
                            upper_half_thickness,
                            lower_half_thickness,
                            polarity};

        for (int y = search_start; y < search_end; ++y) {
            const double seed_score = scorer(options.seed_x, y);
            if (seed_score <= minimum_seed_score) continue;

            HLine line = trace_line(scorer, options.seed_x, y,
                                    seed_score, options);
            if (line.length() >= static_cast<std::size_t>(options.minimum_length)) {
                candidates.push_back({std::move(line)});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [&options](const Candidate& left, const Candidate& right) {
                  if (options.ranking == HLineRanking::Score) {
                      if (left.line.score() != right.line.score())
                          return left.line.score() > right.line.score();
                      return left.line.length() > right.line.length();
                  }
                  if (left.line.length() != right.line.length())
                      return left.line.length() > right.line.length();
                  return left.line.score() > right.line.score();
              });

    const float duplicate_distance = options.duplicate_distance >= 0.0F
        ? options.duplicate_distance
        : static_cast<float>(std::max(1, options.thickness));

    std::vector<HLine> result;
    result.reserve(candidates.size());
    for (Candidate& candidate : candidates) {
        insert_or_merge_hline(result, std::move(candidate.line),
                              duplicate_distance);
    }

    std::sort(result.begin(), result.end(),
              [&options](const HLine& left, const HLine& right) {
                  if (options.ranking == HLineRanking::Score) {
                      if (left.score() != right.score())
                          return left.score() > right.score();
                      return left.length() > right.length();
                  }
                  if (left.length() != right.length())
                      return left.length() > right.length();
                  return left.score() > right.score();
              });
    if (result.size() > maximum_lines) {
        result.erase(result.begin() + static_cast<std::ptrdiff_t>(maximum_lines),
                     result.end());
    }

    return result;
}

std::vector<HLine> find_hlines(const std::vector<std::int16_t>& pixels,
                               int width,
                               int height,
                               const HLineFinderOptions& options,
                               std::size_t maximum_lines) {
    if (width <= 0 || height <= 0 ||
        pixels.size() != static_cast<std::size_t>(width) *
                         static_cast<std::size_t>(height)) {
        throw std::invalid_argument("pixel vector does not match image dimensions");
    }
    return find_hlines(pixels.data(), width, height, options, maximum_lines);
}

std::optional<HLine> find_hline(const std::int16_t* pixels,
                                int width,
                                int height,
                                const HLineFinderOptions& options) {
    std::vector<HLine> lines = find_hlines(pixels, width, height, options, 1);
    if (lines.empty()) return std::nullopt;
    return std::move(lines.front());
}

std::optional<HLine> find_hline(const std::vector<std::int16_t>& pixels,
                                int width,
                                int height,
                                const HLineFinderOptions& options) {
    std::vector<HLine> lines = find_hlines(pixels, width, height, options, 1);
    if (lines.empty()) return std::nullopt;
    return std::move(lines.front());
}
