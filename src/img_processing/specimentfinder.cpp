#include "img_processing/frame.hpp"
#include "img_processing/specimenfinder.hpp"
#include "image_saver.hpp"
#include "processing/processing.hpp"
#include "utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <vector>
#include <iostream>


SpecimenFinder::SpecimenFinder(Config cfg) : FrameProcessor(cfg), readings(SFcfg::minimum_reading_toexit + SFcfg::maximum_disagreeing_readings) {}

#define OVERLAYSCALING 1
#define RAWSCALING 16

void SpecimenFinder::on_setup() {
    
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

void SpecimenFinder::on_termination() {}


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

    struct Guess{
        float x,y,t;

        bool operator==(const Guess& rhs) const {
            return std::abs( (y - rhs.y) / std::max(y,rhs.y) ) < 0.05 && std::abs( (t - rhs.t) / std::max(t,rhs.t) ) < 0.05;
        };
    };

    std::vector<Guess> candidates;

    //we get some guesses
    const int search_column_spacing = width / 2 / SFcfg::number_of_search_column;
    const float max_m = std::tan(SFcfg::max_speciment_tilt * 0.01745);
    const float expected_dist = cfg.pixels_per_mm * SFcfg::specimen_thickness;
    const int vertical_search_interval = expected_dist / 3;

    
    for(int x = width / 2 + search_column_spacing / 2; x < width; x += search_column_spacing){
        BestN<Line> blacklines(2);
        Line whiteline = {{0,0},0,Line::INVALID,0};

        for(int y = 1; y < height / 2 - vertical_search_interval; y += vertical_search_interval){
            const Line blackline = processed_frame.find_hline(x - SFcfg::search_column_width / 2, y, SFcfg::search_column_width, vertical_search_interval,Line::BLACK);
            whiteline = processed_frame.find_hline(x - SFcfg::search_column_width / 2, y, SFcfg::search_column_width, vertical_search_interval,Line::WHITE);

            if( blackline && blackline.strength > 0.5 * SFcfg::search_column_width && abs(blackline.m) < max_m)
                blacklines.insert(blackline);

            if( whiteline && whiteline.strength > 0.5 * SFcfg::search_column_width && abs(whiteline.m) < max_m){
                bool found = false;
                for( Line bl : blacklines)
                    if(whiteline.p.y - bl.p.y  > 0.9 * expected_dist && whiteline.p.y - bl.p.y < 1.1 * expected_dist){
                        candidates.push_back(Guess{ bl.p.x,bl.p.y, whiteline.p.y - bl.p.y});
                        found = true;
                        break;
                    }
                if(found)
                    break;
            }
                
        }

        //candidates.push_back(Guess{ blackline.p.x,blackline.p.y, whiteline.p.y - blackline.p.y});
        #ifdef SF_DRAW
        for( Line l : blacklines)
            for(int dx = - SFcfg::search_column_width / 2; dx < SFcfg::search_column_width / 2; dx++)
                overlay_frame(l.p.x+dx,l.eval(l.p.x+dx)) = 1;

        for(int dx = - SFcfg::search_column_width / 2; dx < SFcfg::search_column_width / 2; dx++)
                overlay_frame(whiteline.p.x+dx,whiteline.eval(whiteline.p.x+dx)) = 1;
            
        #endif
    }

    std::optional<Guess> best_guess = select_best_guess<Guess>(candidates, SFcfg::mininum_number_of_agreeing_guesses, 100000,
    [](const std::vector<Guess>& values){
        float tot_y = 0, tot_t=0, x = 0;
        for(Guess v : values){
            tot_y += v.y;
            tot_t += v.t;
            x = std::max(v.x,x);
        }
        return Guess{x,tot_y/values.size(),tot_t/values.size()};
    });

    if(!best_guess.has_value())
        return ProcessResult::OK;
    
    readings.append({best_guess->y,best_guess->y+best_guess->t,best_guess->t});
   

    return ProcessResult::OK;
}
