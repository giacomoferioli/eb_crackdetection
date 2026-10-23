#include "img_processing/frame.hpp"

#include "utils/best_n.hpp"
#include "utils/grouping.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <vector>


//decentemente controllato
Line Frame::find_hline(int x0, int y0, int w, int h,Line::Type type) {
    if( x0 < FrameCfg::line_search_semi_interval || x0 + w > width() - FrameCfg::line_search_semi_interval ||
        y0 < 1 || y0 + h >= height() ||
        w < 3 || h < 3 || type == Line::Type::INVALID){
            std::cout << "find_hline bad input" << std::endl;
            return {{0,0},0,Line::INVALID,-1};
        }

    if (type == Line::Type::ANY)
        return std::max(find_hline(x0, y0, w, h,Line::Type::WHITE),find_hline(x0, y0, w, h,Line::Type::BLACK) );
    
    //this part is an optimized calculators for line scores
    Frame scores(w,h+2);

    const int l = FrameCfg::line_search_semi_interval;

    //compiles buffer where each pixel store the 7x1 score
    for(int dy = -1; dy <= h; dy++){
        float running_score = 0;

        for(int x = x0 - l; x < x0 + l; x++)
            running_score += (*this)(x,y0+dy);

        for(int dx = 0; dx < w; dx++){
            running_score += (*this)(x0+dx+l,y0+dy);
            scores(dx,dy+1) = running_score;
            running_score -= (*this)(x0+dx-l,y0+dy);
        }
    }
    
    float avg_y_rel = 0;
    float strength = 0;
    //reuses scores(dx,0) to store the point with top y
    for(int dx = 0; dx < w; dx++){
        float topy = 0, top_score = -std::numeric_limits<float>::infinity();
        for(int dy = 0; dy < h; dy ++){
            float score = scores(dx,dy) + scores(dx,dy+1) + scores(dx,dy+2);
            if ( type == Line::Type::BLACK )
                score = - score;
            
            if(score > top_score){
                top_score = score;
                topy = dy;
            }
        }

        scores(dx,0) = topy;
        strength += top_score;
        avg_y_rel += topy;
    }

    avg_y_rel /= w;
    float avg_y = y0 + avg_y_rel;
    float avg_x = x0 + 0.5 * (w-1);

    float m = 0;
    for(int dx = 0; dx < w; dx++){
        m += (dx - 0.5 * (w-1)) * ( scores(dx,0) - avg_y_rel);
    }
    m /= w * ( w * w - 1) / 12.f;

    return {{avg_x,avg_y},m, type, strength};
}

std::optional<SpecimenPositionInfo> Frame::find_specimen(
    const SpecimenSearchParameters& parameters,
    Frame* diagnostic_overlay) {
    std::vector<SpecimenPositionInfo> candidates;

    // [R] Spread the requested number of columns between search_start_x and the right edge.
    const int search_column_spacing =
        (width() - parameters.search_start_x) / parameters.number_of_search_columns;
    const float maximum_slope =
        std::tan(parameters.maximum_tilt_degrees * 0.01745F);
    const int vertical_search_interval = parameters.expected_thickness / 3;

    for(int x = parameters.search_start_x + search_column_spacing / 2;
        x < width();
        x += search_column_spacing) {
        BestN<Line> blacklines(2);
        std::optional<Line> selected_whiteline;

        // [R] Only look for the black and white specimen outlines inside the requested y interval.
        for(int y = parameters.line_search_start_y;
            y < parameters.line_search_end_y - vertical_search_interval;
            y += vertical_search_interval) {
            const Line blackline = find_hline(
                x - parameters.search_column_width / 2,
                y,
                parameters.search_column_width,
                vertical_search_interval,
                Line::BLACK);
            const Line whiteline = find_hline(
                x - parameters.search_column_width / 2,
                y,
                parameters.search_column_width,
                vertical_search_interval,
                Line::WHITE);

            if(blackline &&
               blackline.strength > 0.5F * parameters.search_column_width &&
               std::abs(blackline.m) < maximum_slope)
                blacklines.insert(blackline);

            if(whiteline &&
               whiteline.strength > 0.5F * parameters.search_column_width &&
               std::abs(whiteline.m) < maximum_slope) {
                bool found = false;
                for(const Line& candidate_blackline : blacklines) {
                    const float reference_x =
                        0.5F * (candidate_blackline.p.x + whiteline.p.x);
                    const float thickness =
                        whiteline.eval(reference_x) -
                        candidate_blackline.eval(reference_x);
                    if(std::abs(whiteline.m - candidate_blackline.m) <
                           SpecimenPositionInfo::maximum_slope_difference &&
                       thickness > 0.9F * parameters.expected_thickness &&
                       thickness < 1.1F * parameters.expected_thickness) {
                        Line top = candidate_blackline;
                        top.p = {reference_x, candidate_blackline.eval(reference_x)};
                        candidates.push_back({top, thickness});
                        selected_whiteline = whiteline;
                        found = true;
                        break;
                    }
                }
                if(found)
                    break;
            }
        }

        if(diagnostic_overlay != nullptr) {
            for(const Line& line : blacklines)
                for(int dx = -parameters.search_column_width / 2;
                    dx < parameters.search_column_width / 2;
                    dx++)
                    (*diagnostic_overlay)(
                        static_cast<int>(line.p.x) + dx,
                        static_cast<int>(line.eval(line.p.x + dx))) = 1.0F;

            if(selected_whiteline)
                for(int dx = -parameters.search_column_width / 2;
                    dx < parameters.search_column_width / 2;
                    dx++)
                    (*diagnostic_overlay)(
                        static_cast<int>(selected_whiteline->p.x) + dx,
                        static_cast<int>(selected_whiteline->eval(
                            selected_whiteline->p.x + dx))) = 1.0F;
        }
    }

    std::optional<SpecimenPositionInfo> best_guess =
        select_best_guess<SpecimenPositionInfo>(
            candidates,
            parameters.minimum_agreeing_guesses,
            parameters.maximum_disagreeing_guesses,
            [](const std::vector<SpecimenPositionInfo>& values) {
                SpecimenPositionInfo result = values.front();
                float average_x = 0.0F;
                float average_y = 0.0F;
                float average_thickness = 0.0F;
                float average_strength = 0.0F;
                float average_local_slope = 0.0F;

                for(const SpecimenPositionInfo& value : values) {
                    average_x += value.top.p.x;
                    average_y += value.top.p.y;
                    average_thickness += value.thickness;
                    average_strength += value.top.strength;
                    average_local_slope += value.top.m;
                }
                const float count = static_cast<float>(values.size());
                average_x /= count;
                average_y /= count;
                average_thickness /= count;
                average_strength /= count;
                average_local_slope /= count;

                float xy_deviation = 0.0F;
                float squared_x_deviation = 0.0F;
                for(const SpecimenPositionInfo& value : values) {
                    xy_deviation +=
                        (value.top.p.x - average_x) *
                        (value.top.p.y - average_y);
                    squared_x_deviation +=
                        (value.top.p.x - average_x) *
                        (value.top.p.x - average_x);
                }

                const float slope = squared_x_deviation > 0.0F
                    ? xy_deviation / squared_x_deviation
                    : average_local_slope;
                result.top = {
                    {average_x, average_y},
                    slope,
                    Line::BLACK,
                    average_strength};
                result.thickness = average_thickness;
                result.crackpos = 0.0F;
                return result;
            });

    if(best_guess && std::abs(best_guess->top.m) <= maximum_slope)
        return best_guess;

    return std::nullopt;
}

std::vector<Line> Frame::find_hlines(int x0, int y0, int w, int h,size_t num,Line::Type type) {
    if( x0 < FrameCfg::line_search_semi_interval || x0 + w > width() - FrameCfg::line_search_semi_interval ||
        y0 < 1 || y0 + h >= height() ||
        w < 3 || h < 3 || type == Line::Type::INVALID || type == Line::Type::ANY ||
        num > static_cast<size_t>(h)){
            std::cout << "find_hline bad input" << std::endl;
            return {};
        }
    
    //this part is an optimized calculators for line scores
    Frame scores(w,h+2);

    const int l = FrameCfg::line_search_semi_interval;

    //compiles buffer where each pixel store the 7x1 score
    for(int dy = -1; dy <= h; dy++){
        float running_score = 0;

        for(int x = x0 - l; x < x0 + l; x++)
            running_score += (*this)(x,y0+dy);

        for(int dx = 0; dx < w; dx++){
            running_score += (*this)(x0+dx+l,y0+dy);
            scores(dx,dy+1) = running_score;
            running_score -= (*this)(x0+dx-l,y0+dy);
        }
    }
    
        
    std::vector<LinePoint> candidates;
    //reuses scores(dx,0) to store the point with top y
    for(int dx = 0; dx < w; dx++){
        
        BestN<Entry<int,float>> best(num);
        for(int dy = 0; dy < h; dy ++){
            float score = scores(dx,dy) + scores(dx,dy+1) + scores(dx,dy+2);
            if ( type == Line::Type::BLACK )
                score = - score;
            
            bool insert = true;
            size_t replace_index = best.size();
            for(size_t i = 0; i < best.size(); i++){
                const Entry<int,float>& e = best[i];
                if(std::abs(e.data - dy) <= 3){
                    insert = false;
                    if(score > e.score)
                        replace_index = i;
                    break;
                }
            }

            if(replace_index < best.size()){
                best.remove(replace_index);
                best.insert({dy,score});
            } else if(insert)
                best.insert({dy,score});   
        }

        for(size_t j = 0; j < best.size(); j++)
            candidates.push_back(LinePoint{{(float)dx,(float)best[j].data}});
    }

    /* 
    for( LinePoint p : candidates)
        (*this)(x0 + p.x,y0 + p.y) = 255;
    //*/
    
    BestN<Group<LinePoint>> line_points = select_biggest_groups(candidates, 0.75 * w, num);
    std::vector<Line> lines;
    
    for(const Group<LinePoint>& points : line_points){
        float avg_x_rel = 0;
        float avg_y_rel = 0;
        for(const LinePoint& p : points){
            avg_x_rel += p.x;
            avg_y_rel += p.y;
        }
        avg_x_rel /= points.size();
        avg_y_rel /= points.size();

        const float avg_x = x0 + avg_x_rel;
        float avg_y = y0 + avg_y_rel;

        float xy_deviation = 0;
        float squared_x_deviation = 0;
        for(const LinePoint& p : points){
            xy_deviation += (p.x - avg_x_rel) * (p.y - avg_y_rel);
            squared_x_deviation += (p.x - avg_x_rel) * (p.x - avg_x_rel);
        }
        if(squared_x_deviation == 0)
            continue;

        const float m = xy_deviation / squared_x_deviation;

        lines.push_back({{avg_x,avg_y},m,type,100});
    }

    return lines;
}

/* THIS FUNCTION IS AI GENERATED - IT SEEMS TO WORK BUT FOR NOW IT IS UNUSED BC I STILL HAVE TO REVIEW IT
 * Follows a nearly horizontal ridge from a known point in the requested
 * direction. The response at a candidate point is the same 7-by-3 signed sum
 * used by find_hline. The starting response fixes the line polarity when ANY
 * is requested. At each horizontal step a small beam of paths predicts its
 * next y coordinate, tests nearby rows, rejects slopes outside the configured
 * limit, and penalizes abrupt changes of slope or position. Candidate strength
 * is measured relative to the median and median deviation of its local search
 * band, making the decision less sensitive to the image's absolute event
 * density. Short unsupported gaps are crossed by prediction; after the maximum
 * gap, or at an image boundary, the last confidently supported point is the
 * endpoint returned to the caller.
 */
HLineRide Frame::ride_hline(Point start, HLineDirection direction, Line::Type type) const{
    HLineRide result;

    const int direction_sign = static_cast<int>(direction);
    const int start_x = std::lround(start.x);
    const int start_y = std::lround(start.y);
    const int horizontal_margin = FrameCfg::line_search_semi_interval;

    // Reject points for which the complete response kernel is unavailable.
    if((direction_sign != -1 && direction_sign != 1) ||
       type == Line::Type::INVALID ||
       start_x < horizontal_margin || start_x >= width() - horizontal_margin ||
       start_y < 1 || start_y >= height() - 1)
        return result;

    auto response_at = [&](int x, int y, Line::Type response_type) {
        float response = 0.0F;
        for(int iy = y - 1; iy <= y + 1; iy++)
            for(int ix = x - horizontal_margin; ix <= x + horizontal_margin; ix++)
                response += (*this)(ix,iy);

        return response_type == Line::Type::BLACK ? -response : response;
    };

    auto median = [](std::vector<float> values) {
        const std::size_t middle = values.size() / 2;
        std::nth_element(values.begin(),values.begin() + middle,values.end());
        const float upper = values[middle];
        if(values.size() % 2 != 0)
            return upper;

        std::nth_element(values.begin(),values.begin() + middle - 1,values.begin() + middle);
        return 0.5F * (values[middle - 1] + upper);
    };

    // Lock ANY to the polarity supported at the supplied starting point.
    if(type == Line::Type::ANY){
        const float unsigned_response = response_at(start_x,start_y,Line::Type::WHITE);
        if(unsigned_response == 0.0F)
            return result;
        type = unsigned_response > 0.0F ? Line::Type::WHITE : Line::Type::BLACK;
    }

    auto confidence_in_band = [&](int x, int center_y, int radius, int target_y) {
        const int first_y = std::max(1,center_y - radius);
        const int last_y = std::min(height() - 2,center_y + radius);
        std::vector<float> responses;
        responses.reserve(last_y - first_y + 1);

        float target_response = -std::numeric_limits<float>::infinity();
        for(int y = first_y; y <= last_y; y++){
            const float response = response_at(x,y,type);
            responses.push_back(response);
            if(y == target_y)
                target_response = response;
        }

        if(responses.empty() || !std::isfinite(target_response))
            return -std::numeric_limits<float>::infinity();

        const float baseline = median(responses);
        std::vector<float> deviations;
        deviations.reserve(responses.size());
        for(float response : responses)
            deviations.push_back(std::abs(response - baseline));

        const float noise = std::max(median(deviations),HLineRiderCfg::minimum_noise_scale);
        return (target_response - baseline) / noise;
    };

    // A missing ridge at the anchor is a failed ride, not a zero-length curve.
    const float start_confidence = confidence_in_band(
        start_x,start_y,HLineRiderCfg::noise_sample_radius,start_y);
    if(start_confidence < HLineRiderCfg::minimum_confidence)
        return result;

    struct RiderState {
        int x;
        float y;
        float slope;
        int gap_steps;
        float quality;
        float confidence_sum;
        int confident_steps;
        std::vector<Point> points;
    };

    RiderState initial{
        start_x,start.y,0.0F,0,start_confidence,start_confidence,1,{start}
    };
    std::vector<RiderState> states{std::move(initial)};

    while(!states.empty()){
        std::vector<RiderState> next_states;

        // Expand each surviving path around its slope-based prediction.
        for(const RiderState& state : states){
            const int next_x = state.x + direction_sign * HLineRiderCfg::horizontal_step;
            if(next_x < horizontal_margin || next_x >= width() - horizontal_margin)
                continue;

            const float predicted_y = state.y + state.slope * direction_sign * HLineRiderCfg::horizontal_step;
            const int prediction_row = std::lround(predicted_y);
            const int first_y = std::max(1,prediction_row - HLineRiderCfg::search_radius);
            const int last_y = std::min(height() - 2,prediction_row + HLineRiderCfg::search_radius);
            bool found_supported_candidate = false;

            for(int candidate_y = first_y; candidate_y <= last_y; candidate_y++){
                const float candidate_slope =
                    (candidate_y - state.y) / (direction_sign * HLineRiderCfg::horizontal_step);
                if(std::abs(candidate_slope) >= HLineRiderCfg::max_abs_slope)
                    continue;

                const float confidence = confidence_in_band(
                    next_x,prediction_row,HLineRiderCfg::noise_sample_radius,candidate_y);
                if(confidence < HLineRiderCfg::minimum_confidence)
                    continue;

                found_supported_candidate = true;
                const float slope_change = std::abs(candidate_slope - state.slope);
                const float prediction_error = std::abs(candidate_y - predicted_y);

                RiderState candidate = state;
                candidate.x = next_x;
                candidate.y = static_cast<float>(candidate_y);
                candidate.slope =
                    (1.0F - HLineRiderCfg::slope_update_weight) * state.slope +
                    HLineRiderCfg::slope_update_weight * candidate_slope;
                candidate.gap_steps = 0;
                candidate.quality += confidence
                    - HLineRiderCfg::slope_change_penalty * slope_change
                    - HLineRiderCfg::position_penalty * prediction_error;
                candidate.confidence_sum += confidence;
                candidate.confident_steps++;
                candidate.points.push_back({static_cast<float>(next_x),static_cast<float>(candidate_y)});
                next_states.push_back(std::move(candidate));
            }

            // Preserve the predicted path briefly when the image has a small gap.
            if(!found_supported_candidate && state.gap_steps + 1 < HLineRiderCfg::maximum_gap_steps &&
               predicted_y >= 1.0F && predicted_y < height() - 1.0F){
                RiderState gap = state;
                gap.x = next_x;
                gap.y = predicted_y;
                gap.gap_steps++;
                gap.quality -= HLineRiderCfg::gap_penalty;
                next_states.push_back(std::move(gap));
            }
        }

        if(next_states.empty())
            break;

        // Retain several alternatives so one noisy choice cannot capture the ride.
        std::sort(next_states.begin(),next_states.end(),[](const RiderState& lhs, const RiderState& rhs) {
            return lhs.quality > rhs.quality;
        });
        if(next_states.size() > static_cast<std::size_t>(HLineRiderCfg::beam_width))
            next_states.resize(HLineRiderCfg::beam_width);
        states = std::move(next_states);
    }

    if(states.empty())
        return result;

    // The strongest surviving history ends at its last supported, not predicted, point.
    const RiderState& best = *std::max_element(states.begin(),states.end(),
        [](const RiderState& lhs, const RiderState& rhs) {
            return lhs.quality < rhs.quality;
        });
    result.points = best.points;
    result.endpoint = best.points.back();
    result.type = type;
    result.confidence = best.confidence_sum / best.confident_steps;
    return result;
}
