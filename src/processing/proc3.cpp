#include "processing/proc3.hpp"
#include "image_saver.hpp"
#include "processing/hline4.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

// Maximum number of pixels past both detected line ends where a crossing may be accepted.
#define P3_GID_MAX_INTERSECTION_RIGHT_DISTANCE 100

namespace {
// ImageSaver expects pixels packed as 0xRRGGBBAA.
constexpr std::uint32_t BACKGROUND = 0x808080FF;
constexpr std::uint32_t WHITE = 0xFFFFFFFF;
constexpr std::uint32_t BLACK = 0x000000FF;
constexpr std::uint32_t RED = 0xFF0000FF;
constexpr std::uint32_t BLUE = 0x0000FFFF;

double polynomial_y(const HLine4Polynomial& polynomial,double x,int){
    x = (x - polynomial.x_centre) / polynomial.x_scale;
    // Horner's method evaluates the polynomial with few multiplications.
    const auto& c = polynomial.coefficients;
    return (((c[4] * x + c[3]) * x + c[2]) * x + c[1]) * x + c[0];
}

// Only the sign matters here. Normalizing x changes the magnitude of the
// second derivative, but not its sign.
double polynomial_curvature(const HLine4Polynomial& polynomial,double x,int){
    x = (x - polynomial.x_centre) / polynomial.x_scale;
    const auto& c = polynomial.coefficients;
    return (2 * c[2] + 6 * c[3] * x + 12 * c[4] * x * x) /
           (polynomial.x_scale * polynomial.x_scale);
}

bool find_intersection(const HLine& first,const HLine& second,
                       const HLine4Polynomial& first_polynomial,
                       const HLine4Polynomial& second_polynomial,
                       int width,double& intersection_x,double& intersection_y){
    int start = std::max(first.start_x(),second.start_x());
    int overlap_end = std::min(first.end_x(),second.end_x());
    if(start >= overlap_end) return false;

    // Both fits may be extrapolated this far past the end of their measured
    // data. Using the earlier end guarantees the crossing is within the limit
    // for both lines, even when their detected domains end at different x.
    int search_end = std::min(width - 1,overlap_end + P3_GID_MAX_INTERSECTION_RIGHT_DISTANCE);

    // Quartics can cross more than once. The closest approach of the measured
    // lines tells us which polynomial root belongs to the observed crossing.
    int closest_x = start;
    double closest_distance = std::numeric_limits<double>::infinity();
    for(int x = start; x <= overlap_end; x++){
        double distance = std::abs(first.y_at(x) - second.y_at(x));
        if(distance < closest_distance){
            closest_distance = distance;
            closest_x = x;
        }
    }

    // Scan through the measured overlap and the allowed extrapolation range
    // one pixel at a time for sign changes in first(x)-second(x).
    bool found = false;
    double best_distance = std::numeric_limits<double>::infinity();
    double previous_x = start;
    double previous_difference = polynomial_y(first_polynomial,previous_x,width) - polynomial_y(second_polynomial,previous_x,width);
    for(int x = start + 1; x <= search_end; x++){
        double difference = polynomial_y(first_polynomial,x,width) - polynomial_y(second_polynomial,x,width);
        bool crosses = (previous_difference < 0) != (difference < 0);
        if(previous_difference == 0 || difference == 0 || crosses){
            double left = previous_x,right = x;
            double root;
            if(previous_difference == 0) root = previous_x;
            else if(difference == 0) root = x;
            else{
                // A sign change brackets a root. Bisection refines its x value.
                double left_difference = previous_difference;
                for(int i = 0; i < 30; i++){
                    double middle = (left + right) / 2;
                    double middle_difference = polynomial_y(first_polynomial,middle,width) - polynomial_y(second_polynomial,middle,width);
                    if((left_difference < 0) != (middle_difference < 0)) right = middle;
                    else{
                        left = middle;
                        left_difference = middle_difference;
                    }
                }
                root = (left + right) / 2;
            }
            double distance = std::abs(root - closest_x);
            if(distance < best_distance){
                best_distance = distance;
                intersection_x = root;
                intersection_y = (polynomial_y(first_polynomial,root,width) + polynomial_y(second_polynomial,root,width)) / 2;
                found = true;
            }
        }
        previous_x = x;
        previous_difference = difference;
    }
    return found;
}

bool matches_geometry(const HLine4Polynomial& white_polynomial,const HLine4Polynomial& black_polynomial,int width,int overlap_start,double intersection_x){
    if(intersection_x <= overlap_start + 1) return false;

    // Before the crossing the white curve must be above the black curve.
    // Since image y grows downwards, that means black_y - white_y is positive.
    // Comparing the gap at 20% and 80% verifies that the curves converge.
    double first_x = overlap_start + (intersection_x - overlap_start) * 0.2;
    double second_x = overlap_start + (intersection_x - overlap_start) * 0.8;
    double first_gap = polynomial_y(black_polynomial,first_x,width) - polynomial_y(white_polynomial,first_x,width);
    double second_gap = polynomial_y(black_polynomial,second_x,width) - polynomial_y(white_polynomial,second_x,width);
    if(first_gap <= 0 || second_gap < 0 || second_gap >= first_gap) return false;

    // Image y grows downwards: a visually concave upper line has positive
    // second derivative, while a visually convex lower line has negative one.
    for(int sample = 1; sample <= 3; sample++){
        double x = overlap_start + (intersection_x - overlap_start) * sample / 4;
        if(polynomial_curvature(white_polynomial,x,width) >= 0 ||
           polynomial_curvature(black_polynomial,x,width) <= 0) return false;
    }
    return true;
}
}

Proc3::Proc3(EventSource& source, Config cfg) : Processing(source,cfg)/*,f_displacement("data/displ.csv")*/ {
    
}

// Number of evenly spaced seed columns used to find partial-width lines.
#define P3_GID_LINEFINDER_N 5
// Maximum positive-polarity candidates retained after merging all seed scans.
#define P3_GID_MAXLINES_WHITE 5
// Maximum negative-polarity candidates retained after merging all seed scans.
#define P3_GID_MAXLINES_BLACK 4

void Proc3::on_setup() {
    // Proc3 owns an RGB output frame. ImageSaver reads one packed pixel through
    // this callback whenever Processing asks it to save the current result.
    frame.assign(width*height,BACKGROUND);
    img_saver.reset(new ImageSaver(
    [this] (int ix) { return frame[ix]; },
    width,height,"proc_images/"
    ));
}

void Proc3::on_termination() {
    //f_displacement.close();
}

std::vector<HLine4> Proc3::find_lines(HLinePolarity polarity, int n) {
    std::vector<HLine4> lines;
    if (n <= 0) return lines;

    const int first_x = HLINE4_SCORING_HALF_WIDTH;
    const int last_x = width - HLINE4_SCORING_HALF_WIDTH - 1;
    if (first_x > last_x) return lines;

    // find_hlines4 only finds lines crossing seed_x. Scan several seed columns
    // so lines that do not span the complete image can still be discovered.
    for (int scan = 0; scan < P3_GID_LINEFINDER_N; ++scan) {
        const int x = P3_GID_LINEFINDER_N == 1
            ? (first_x + last_x) / 2
            : first_x + (last_x - first_x) * scan / (P3_GID_LINEFINDER_N - 1);
        auto found = find_hlines4(
            get_raw_frame().data(),
            width,
            height,
            x,
            polarity,
            n
        );

        for (HLine4& candidate : found) {
            // Detections from different seed columns may describe the same
            // physical line; merge those before ranking the result.
            insert_or_merge_hline4(lines,std::move(candidate));
        }
    }

    // Keep at most n candidates. Geometry, not this ordering, selects the final
    // white/black pair later in general_identification().
    std::sort(lines.begin(), lines.end(),
              [](const HLine4& first, const HLine4& second) {
                  if (first.length() != second.length())
                      return first.length() > second.length();
                  return first.score() > second.score();
              });
    if (lines.size() > static_cast<std::size_t>(n)) {
        lines.erase(lines.begin() + n, lines.end());
    }
    return lines;
}

void Proc3::general_identification(){
    // Positive event response represents white; negative represents black.
    std::vector<HLine4> white_lines = find_lines(HLinePolarity::Positive,P3_GID_MAXLINES_WHITE);
    std::vector<HLine4> black_lines = find_lines(HLinePolarity::Negative,P3_GID_MAXLINES_BLACK);

    // Show every candidate even when no pair passes the geometry tests.
    for(const HLine4& line : white_lines)
        for(int x = line.start_x(); x <= line.end_x(); x++)
            pix(x,std::lround(line.y_at(x))) = WHITE;
    for(const HLine4& line : black_lines)
        for(int x = line.start_x(); x <= line.end_x(); x++)
            pix(x,std::lround(line.y_at(x))) = BLACK;

    if(white_lines.empty() || black_lines.empty()) return;

    // These remain null until a candidate pair passes every geometry test.
    const HLine* white_line = nullptr;
    const HLine* black_line = nullptr;
    HLine4Polynomial white_polynomial,black_polynomial;
    double intersection_x = 0,intersection_y = 0;
    double best_score = -std::numeric_limits<double>::infinity();

    // Geometry identifies the wanted pair; detector score only breaks ties
    // between pairs that satisfy the requested shape and ordering.
    for(const HLine4& white_candidate : white_lines){
        const HLine4Polynomial& candidate_white_polynomial = white_candidate.polynomial();
        for(std::size_t black_index = 0; black_index < black_lines.size(); black_index++){
            const HLine4& black_candidate = black_lines[black_index];
            const HLine4Polynomial& candidate_black_polynomial = black_candidate.polynomial();
            int overlap_start = std::max(white_candidate.start_x(),black_candidate.start_x());
            double candidate_x = 0,candidate_y = 0;
            // A usable pair must cross no more than the configured distance
            // after both lines, have white above black, converge, and possess
            // the requested curvature.
            if(!find_intersection(white_candidate,black_candidate,candidate_white_polynomial,candidate_black_polynomial,
                                  width,candidate_x,candidate_y) ||
               !matches_geometry(candidate_white_polynomial,candidate_black_polynomial,width,overlap_start,candidate_x)) continue;

            double score = white_candidate.score() + black_candidate.score();
            if(score <= best_score) continue;
            best_score = score;
            white_line = &white_candidate;
            black_line = &black_candidate;
            white_polynomial = candidate_white_polynomial;
            black_polynomial = candidate_black_polynomial;
            intersection_x = candidate_x;
            intersection_y = candidate_y;
        }
    }
    if(!white_line) return;

    // Draw only the selected quartic fits in red, including extrapolation.
    int polynomial_end = std::min(width - 1,(int)std::ceil(intersection_x));
    for(int x = white_line->start_x(); x <= polynomial_end; x++){
        int y = std::lround(polynomial_y(white_polynomial,x,width));
        if(y >= 0 && y < height) pix(x,y) = RED;
    }
    for(int x = black_line->start_x(); x <= polynomial_end; x++){
        int y = std::lround(polynomial_y(black_polynomial,x,width));
        if(y >= 0 && y < height) pix(x,y) = RED;
    }

    // A radius-three blue disk makes the selected crossing easy to see.
    int centre_x = std::lround(intersection_x),centre_y = std::lround(intersection_y);
    for(int dy = -3; dy <= 3; dy++)
        for(int dx = -3; dx <= 3; dx++)
            if(dx*dx + dy*dy <= 9 && centre_x+dx >= 0 && centre_x+dx < width && centre_y+dy >= 0 && centre_y+dy < height)
                pix(centre_x+dx,centre_y+dy) = BLUE;
}

void Proc3::process_frame(unsigned framenum){
    // Clear the previous result, analyse the current signed event frame, then
    // Processing/ImageSaver can consume the RGB frame produced above.
    frame.assign(height * width,BACKGROUND);

    general_identification();

}
