#include "img_processing/specimenfinder.hpp"

#include "image_saver.hpp"
#include "utils/best_n.hpp"
#include "utils/grouping.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>


SpecimenFinder::SpecimenFinder(Config cfg) : FrameProcessor(cfg) {}

#define OVERLAYSCALING 1
#define RAWSCALING 16

void SpecimenFinder::on_setup(unsigned) {
    
    processed_frame.reset(width, height, 0.0F);
    #if SF_DRAW
    overlay_frame.reset(width, height, 0.0F);
    img_saver.reset(new ImageSaver(
    [this] (int ix) { 
        int gray = std::clamp((int)std::round(processed_frame[ix] * RAWSCALING + 128),0,255);
        float alpha = std::clamp(overlay_frame[ix] * OVERLAYSCALING,0.0F,1.0F);
        unsigned int red = std::round(alpha * 255 + (1 - alpha) * gray);
        unsigned int green_blue = std::round((1 - alpha) * gray);

        return red << 24U | green_blue << 16U | green_blue << 8U | 0xFF;
    },
    width,height,"proc_images/"
    ));
    #endif
}

void SpecimenFinder::on_termination(unsigned) {}


void SpecimenFinder::procFrame(){
    processed_frame.fill(0.0F);
    //gaussian blur
    //kills hot pixels
    Frame raw_frame(width, height);
    const BasicFrame<std::int16_t>& raw_pixels = get_raw_frame();
    std::copy(raw_pixels.begin(), raw_pixels.end(), raw_frame.begin());
    
    [[maybe_unused]] int c = 0;
    for(int j = 1; j < height - 1; j++){
        for(int i = 1; i < width - 1; i++){
            float avg = (float)(abs(raw_frame(i - 1, j)) + abs(raw_frame(i + 1, j)) + abs(raw_frame(i, j - 1)) + abs(raw_frame(i, j + 1)))/4;

            if(abs(raw_frame(i, j)) > 5 && abs(raw_frame(i, j)) > 8 * avg){
                raw_frame(i, j) = raw_frame(i, j) > 0 ? std::round(avg) : -std::round(avg);
                c++;
            }
        }
    }        
    //std::cout << c << std::endl;
    
    for(int j = 0; j < height; j++){
        for(int i = 2; i < width - 2; i++){
            
            processed_frame(i,j) = ( raw_frame(i-2,j) +  4.f * raw_frame(i-1,j) + 6.f * raw_frame(i,j) + 4.f * raw_frame(i+1,j) + raw_frame(i+2,j)) / 16.f;
            /*processed_frame(i, j) = (
                12.f * raw_frame(i, j)
                + 2.f * (raw_frame(i + 1, j) + raw_frame(i - 1, j) + raw_frame(i, j + 1) + raw_frame(i, j - 1))
                + 1.f * (raw_frame(i + 1, j + 1) + raw_frame(i - 1, j + 1) + raw_frame(i - 1, j - 1) + raw_frame(i + 1, j - 1))
                ) / 24.f;
            */
        }
    }
}           



ProcessResult SpecimenFinder::process_frame(unsigned framenum){
    [[maybe_unused]]float t = framenum * cfg.processing_interval;
    #if SF_DRAW
    overlay_frame.fill(0.0F);
    #endif 
       
    procFrame();

    std::vector<SpecimenPositionInfo> candidates;

    //we get some guesses
    const int search_column_spacing = width / 2 / SFcfg::number_of_search_column;
    const float max_m = std::tan(SFcfg::max_speciment_tilt * 0.01745);
    const float expected_dist = cfg.pixels_per_mm * SFcfg::specimen_thickness;
    const int vertical_search_interval = expected_dist / 3;

    
    for(int x = width / 2 + search_column_spacing / 2; x < width; x += search_column_spacing){
        BestN<Line> blacklines(2);
        std::optional<Line> selected_whiteline;

        for(int y = 1; y < height / 2 - vertical_search_interval; y += vertical_search_interval){
            const Line blackline = processed_frame.find_hline(x - SFcfg::search_column_width / 2, y, SFcfg::search_column_width, vertical_search_interval,Line::BLACK);
            const Line whiteline = processed_frame.find_hline(x - SFcfg::search_column_width / 2, y, SFcfg::search_column_width, vertical_search_interval,Line::WHITE);

            if(blackline && blackline.strength > 0.5F * SFcfg::search_column_width && std::abs(blackline.m) < max_m)
                blacklines.insert(blackline);

            if(whiteline && whiteline.strength > 0.5F * SFcfg::search_column_width && std::abs(whiteline.m) < max_m){
                bool found = false;
                for(const Line& bl : blacklines){
                    const float refx = 0.5F * (bl.p.x + whiteline.p.x);
                    const float thickness = whiteline.eval(refx) - bl.eval(refx);
                    if(std::abs(whiteline.m - bl.m) < SpecimenPositionInfo::maximum_slope_difference &&
                       thickness > 0.9F * expected_dist && thickness < 1.1F * expected_dist){
                        Line top = bl;
                        top.p = {refx,bl.eval(refx)};
                        candidates.push_back({top,thickness});
                        selected_whiteline = whiteline;
                        found = true;
                        break;
                    }
                }
                if(found)
                    break;
            }
                
        }

        //candidates.push_back(Guess{ blackline.p.x,blackline.p.y, whiteline.p.y - blackline.p.y});
        #ifdef SF_DRAW
        for(const Line& l : blacklines)
            for(int dx = - SFcfg::search_column_width / 2; dx < SFcfg::search_column_width / 2; dx++)
                overlay_frame(l.p.x+dx,l.eval(l.p.x+dx)) = 1;

        if(selected_whiteline)
            for(int dx = - SFcfg::search_column_width / 2; dx < SFcfg::search_column_width / 2; dx++)
                overlay_frame(selected_whiteline->p.x+dx,selected_whiteline->eval(selected_whiteline->p.x+dx)) = 1;
            
        #endif
    }

    std::optional<SpecimenPositionInfo> best_guess = select_best_guess<SpecimenPositionInfo>(
        candidates,
        SFcfg::mininum_number_of_agreeing_guesses,
        SFcfg::maximum_number_of_disagreeing_guesses,
        [](const std::vector<SpecimenPositionInfo>& values){
            SpecimenPositionInfo result = values.front();
            float avg_x = 0.0F;
            float avg_y = 0.0F;
            float avg_thickness = 0.0F;
            float avg_strength = 0.0F;
            float avg_local_slope = 0.0F;

            for(const SpecimenPositionInfo& value : values){
                avg_x += value.top.p.x;
                avg_y += value.top.p.y;
                avg_thickness += value.thickness;
                avg_strength += value.top.strength;
                avg_local_slope += value.top.m;
            }
            const float count = static_cast<float>(values.size());
            avg_x /= count;
            avg_y /= count;
            avg_thickness /= count;
            avg_strength /= count;
            avg_local_slope /= count;

            float xy_deviation = 0.0F;
            float squared_x_deviation = 0.0F;
            for(const SpecimenPositionInfo& value : values){
                xy_deviation += (value.top.p.x - avg_x) * (value.top.p.y - avg_y);
                squared_x_deviation += (value.top.p.x - avg_x) * (value.top.p.x - avg_x);
            }

            const float slope = squared_x_deviation > 0.0F
                ? xy_deviation / squared_x_deviation
                : avg_local_slope;
            result.top = {{avg_x,avg_y},slope,Line::BLACK,avg_strength};
            result.thickness = avg_thickness;
            result.crackpos = 0.0F;
            return result;
        });

    if(!best_guess.has_value() || std::abs(best_guess->top.m) > max_m)
        return ProcessResult::OK;

    // [R] Retain the newest successful specimen estimate for subsequent
    // processors; the frame number lets consumers detect stale handoff data.
    shared_data().specimen_position = *best_guess;
    shared_data().specimen_position_frame = framenum;

    #if SF_DRAW
    for(int x = 0; x < width; x++){
        const int top_y = std::lround(best_guess->top.eval(static_cast<float>(x)));
        const int bottom_y = std::lround(best_guess->top.eval(static_cast<float>(x)) + best_guess->thickness);
        if(top_y >= 0 && top_y < height)
            overlay_frame(x,top_y) += 0.5F;
        if(bottom_y >= 0 && bottom_y < height)
            overlay_frame(x,bottom_y) += 0.5F;
    }
    #endif

    this->shared_data().specimen_position = best_guess.value();
    return ProcessResult::DONE;
}
