#include "processing/hline4.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <opencv2/core.hpp>

namespace {

double evaluate_polynomial(const HLine4Polynomial& polynomial,double x){
    x = (x - polynomial.x_centre) / polynomial.x_scale;
    double y = polynomial.coefficients[HLINE4_POLYNOMIAL_DEGREE];
    for(int degree = HLINE4_POLYNOMIAL_DEGREE - 1; degree >= 0; degree--)
        y = y * x + polynomial.coefficients[degree];
    return y;
}

double polynomial_curvature(const HLine4Polynomial& polynomial,double normalized_x){
    const auto& c = polynomial.coefficients;
    return (2 * c[2] + 6 * c[3] * normalized_x + 12 * c[4] * normalized_x * normalized_x) /
           (polynomial.x_scale * polynomial.x_scale);
}

void enforce_curvature_sign(HLine4Polynomial& polynomial){
    auto& c = polynomial.coefficients;
    // p''(x) = 12*c4*x² + 6*c3*x + 2*c2. Its sign is constant for every x
    // when c2 and c4 have the same sign and c2*c4 >= 3*c3²/8.
    double centre_curvature = 2 * c[2];
    int sign = centre_curvature < 0 || (centre_curvature == 0 && c[4] < 0) ? -1 : 1;
    double fourth = std::abs(c[4]);
    if(fourth < HLINE4_MINIMUM_QUARTIC_COEFFICIENT){
        c[2] = sign * std::abs(c[2]);
        c[3] = 0;
        c[4] = 0;
        return;
    }

    c[4] = sign * fourth;
    double minimum_second = 3 * c[3] * c[3] / (8 * fourth);
    c[2] = sign * std::max(std::abs(c[2]),minimum_second);
}

void cap_polynomial_curvature(HLine4Polynomial& polynomial,
                              double minimum_x,double maximum_x){
    minimum_x = (minimum_x - polynomial.x_centre) / polynomial.x_scale;
    maximum_x = (maximum_x - polynomial.x_centre) / polynomial.x_scale;
    // The second derivative is quadratic, so its largest absolute value in the
    // requested x-range is at an endpoint or at its stationary point.
    double maximum = std::max(std::abs(polynomial_curvature(polynomial,minimum_x)),
                              std::abs(polynomial_curvature(polynomial,maximum_x)));
    if(polynomial.coefficients[4] != 0){
        double stationary_x = -polynomial.coefficients[3] / (4 * polynomial.coefficients[4]);
        if(stationary_x > minimum_x && stationary_x < maximum_x)
            maximum = std::max(maximum,std::abs(polynomial_curvature(polynomial,stationary_x)));
    }
    if(maximum <= HLINE4_MAX_CURVATURE) return;

    double reduction = HLINE4_MAX_CURVATURE / maximum;
    for(int degree = 2; degree <= HLINE4_POLYNOMIAL_DEGREE; degree++)
        polynomial.coefficients[degree] *= reduction;
}

HLine4Polynomial fit_polynomial(const std::vector<double>& xs,
                                const std::vector<float>& ys,
                                double curvature_start,double curvature_end){
    auto limits = std::minmax_element(xs.begin(),xs.end());
    HLine4Polynomial result;
    result.x_centre = (*limits.first + *limits.second) / 2;
    result.x_scale = std::max(1.0,(*limits.second - *limits.first) / 2);
    if(xs.size() < HLINE4_COEFFICIENT_COUNT){
        result.coefficients[0] = ys.front();
        return result;
    }

    cv::Mat powers((int)xs.size(),HLINE4_COEFFICIENT_COUNT,CV_64F);
    cv::Mat values((int)xs.size(),1,CV_64F),coefficients;
    for(int row = 0; row < (int)xs.size(); row++){
        double x = (xs[row] - result.x_centre) / result.x_scale,power = 1;
        values.at<double>(row) = ys[row];
        for(int degree = 0; degree < HLINE4_COEFFICIENT_COUNT; degree++){
            powers.at<double>(row,degree) = power;
            power *= x;
        }
    }
    cv::solve(powers,values,coefficients,cv::DECOMP_SVD);

    for(int degree = 0; degree < HLINE4_COEFFICIENT_COUNT; degree++)
        result.coefficients[degree] = coefficients.at<double>(degree);
    enforce_curvature_sign(result);
    cap_polynomial_curvature(result,curvature_start,curvature_end);
    return result;
}

HLine4Polynomial fit_line(const HLine& line){
    std::vector<double> xs(line.length());
    for(std::size_t i = 0; i < line.length(); i++) xs[i] = line.start_x() + i;
    return fit_polynomial(xs,line.points(),line.start_x(),line.end_x());
}

HLine4Polynomial fit_line(const HLine& line,int image_width){
    std::vector<double> xs(line.length());
    for(std::size_t i = 0; i < line.length(); i++) xs[i] = line.start_x() + i;
    return fit_polynomial(xs,line.points(),0,image_width - 1);
}

struct Scorer {
    const std::int16_t* pixels;
    int width;
    int height;
    int upper_half_thickness;
    int lower_half_thickness;
    int polarity;

    double operator()(int x,int y) const {
        if(x < HLINE4_SCORING_HALF_WIDTH || x >= width - HLINE4_SCORING_HALF_WIDTH ||
           y < upper_half_thickness || y >= height - lower_half_thickness)
            return -std::numeric_limits<double>::infinity();

        std::int64_t sum = 0;
        for(int sample_x = x - HLINE4_SCORING_HALF_WIDTH;
            sample_x <= x + HLINE4_SCORING_HALF_WIDTH; sample_x++)
            for(int sample_y = y - upper_half_thickness;
                sample_y <= y + lower_half_thickness; sample_y++)
                sum += pixels[sample_y * width + sample_x];
        return polarity * (double)sum;
    }
};

struct Extension {
    std::vector<float> points;
    double score_sum = 0;
    int detected_points = 0;
};

double extension_reference_score(const Scorer& scorer,const HLine& line,int direction){
    int x = direction < 0 ? line.start_x() : line.end_x();
    double score = 0;
    for(int sample = 0; sample < HLINE4_REFERENCE_SAMPLE_COUNT; sample++,x -= direction)
        score += scorer(x,std::lround(line.y_at(x)));
    return score / HLINE4_REFERENCE_SAMPLE_COUNT;
}

Extension extend_line(const Scorer& scorer,const HLine& line,
                      const HLine4Polynomial& polynomial,int direction){
    Extension result;
    std::vector<float> gap;
    int end_x = direction < 0 ? line.start_x() : line.end_x();
    double reference_score = extension_reference_score(scorer,line,direction);

    for(int x = end_x + direction;
        x >= HLINE4_SCORING_HALF_WIDTH && x < scorer.width - HLINE4_SCORING_HALF_WIDTH;
        x += direction){
        double prediction = evaluate_polynomial(polynomial,x);
        int centre_y = std::lround(prediction);
        int best_y = centre_y;
        double best_score = -std::numeric_limits<double>::infinity();
        for(int delta_y = -HLINE4_EXTENSION_SEARCH_RADIUS;
            delta_y <= HLINE4_EXTENSION_SEARCH_RADIUS; delta_y++){
            int candidate_y = centre_y + delta_y;
            double candidate_score = scorer(x,candidate_y);
            if(candidate_score > best_score){
                best_score = candidate_score;
                best_y = candidate_y;
            }
        }

        if(best_score <= reference_score * HLINE4_CONTINUATION_RATIO){
            gap.push_back(prediction);
            if(gap.size() > HLINE4_MAX_EXTENSION_GAP) break;
            continue;
        }

        // Keep short missing sections only when a real continuation is found.
        result.points.insert(result.points.end(),gap.begin(),gap.end());
        gap.clear();
        float y = HLINE4_POSITION_INERTIA * prediction +
                  (1.0 - HLINE4_POSITION_INERTIA) * best_y;
        result.points.push_back(y);
        result.score_sum += best_score;
        result.detected_points++;
    }
    return result;
}

HLine4 extend_line(const Scorer& scorer,const HLine& line,HLine4Polynomial polynomial){
    Extension left = extend_line(scorer,line,polynomial,-1);
    Extension right = extend_line(scorer,line,polynomial,1);

    std::vector<float> points;
    points.reserve(left.points.size() + line.length() + right.points.size());
    points.insert(points.end(),left.points.rbegin(),left.points.rend());
    points.insert(points.end(),line.points().begin(),line.points().end());
    points.insert(points.end(),right.points.begin(),right.points.end());

    int scored_points = line.length() + left.detected_points + right.detected_points;
    double mean_score = (line.score() * line.length() + left.score_sum + right.score_sum) /
                        scored_points;
    int start_x = line.start_x() - (int)left.points.size();
    return HLine4(start_x,std::move(points),std::move(polynomial),mean_score);
}

} // namespace

HLine4::HLine4(int start_x,std::vector<float> points,double score)
    : HLine(start_x,std::move(points),score),polynomial_(fit_line(*this)) {}

HLine4::HLine4(int start_x,std::vector<float> points,HLine4Polynomial polynomial,double score)
    : HLine(start_x,std::move(points),score),polynomial_(std::move(polynomial)) {}

double HLine4::polynomial_y(double x) const {
    return evaluate_polynomial(polynomial_,x);
}

bool hlines4_are_same(const HLine4& first,const HLine4& second){
    int start = std::max(first.start_x(),second.start_x());
    int end = std::min(first.end_x(),second.end_x());
    if(start > end) return false;

    double distance = 0;
    for(int x = start; x <= end; x++) distance += std::abs(first.y_at(x) - second.y_at(x));
    return distance / (end - start + 1) < HLINE4_DUPLICATE_DISTANCE;
}

HLine4 merge_hlines4(const HLine4& first,const HLine4& second){
    const HLine4* preferred = &first;
    const HLine4* other = &second;
    if(second.length() > first.length() ||
       (second.length() == first.length() && second.score() > first.score())){
        preferred = &second;
        other = &first;
    }

    int start = std::min(first.start_x(),second.start_x());
    int end = std::max(first.end_x(),second.end_x());
    std::vector<float> points;
    points.reserve(end - start + 1);
    for(int x = start; x <= end; x++)
        points.push_back(preferred->contains(x) ? preferred->y_at(x) : other->y_at(x));

    double score = (first.score() * first.length() + second.score() * second.length()) /
                   (first.length() + second.length());
    return HLine4(start,std::move(points),preferred->polynomial(),score);
}

void insert_or_merge_hline4(std::vector<HLine4>& lines,HLine4 candidate){
    for(std::size_t index = 0; index < lines.size();){
        if(!hlines4_are_same(candidate,lines[index])){
            index++;
            continue;
        }
        candidate = merge_hlines4(candidate,lines[index]);
        lines.erase(lines.begin() + index);
        index = 0;
    }
    lines.push_back(std::move(candidate));
}

std::vector<HLine4> find_hlines4(const std::int16_t* pixels,int width,int height,
                                 int seed_x,HLinePolarity polarity,std::size_t maximum_lines){
    HLineFinderOptions options;
    options.seed_x = seed_x;
    options.polarity = polarity;
    options.thickness = HLINE4_LINE_THICKNESS;
    options.scoring_half_width = HLINE4_SCORING_HALF_WIDTH;
    options.minimum_length = HLINE4_MINIMUM_LENGTH;
    options.max_vertical_step = HLINE4_INITIAL_MAX_VERTICAL_STEP;
    options.minimum_seed_score = HLINE4_MINIMUM_SEED_SCORE;
    options.continuation_ratio = HLINE4_CONTINUATION_RATIO;
    options.reference_sample_count = HLINE4_REFERENCE_SAMPLE_COUNT;
    options.inertia = HLINE4_POSITION_INERTIA;
    options.duplicate_distance = HLINE4_DUPLICATE_DISTANCE;

    // Phase one is exactly the original local HLine finder.
    std::vector<HLine> initial_lines = find_hlines(pixels,width,height,options,maximum_lines);
    int upper_half_thickness = (HLINE4_LINE_THICKNESS - 1) / 2;
    int lower_half_thickness = HLINE4_LINE_THICKNESS / 2;
    std::vector<HLine4> lines;
    lines.reserve(initial_lines.size());
    for(const HLine& line : initial_lines){
        // This is the only polynomial fit performed for this line search.
        HLine4Polynomial polynomial = fit_line(line,width);
        int sign = polarity == HLinePolarity::Negative ? -1 : 1;
        if(polarity == HLinePolarity::Either){
            Scorer unsigned_scorer{pixels,width,height,upper_half_thickness,lower_half_thickness,1};
            sign = unsigned_scorer(seed_x,std::lround(line.y_at(seed_x))) < 0 ? -1 : 1;
        }
        Scorer scorer{pixels,width,height,upper_half_thickness,lower_half_thickness,sign};
        lines.push_back(extend_line(scorer,line,std::move(polynomial)));
    }

    std::sort(lines.begin(),lines.end(),[](const HLine4& first,const HLine4& second){
        if(first.length() != second.length()) return first.length() > second.length();
        return first.score() > second.score();
    });
    if(lines.size() > maximum_lines) lines.erase(lines.begin() + maximum_lines,lines.end());
    return lines;
}
