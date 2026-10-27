#include "processors/specimenfinder.hpp"

#include "image_saver.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>


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

    const SpecimenSearchParameters search_parameters{
        SFcfg::number_of_search_column,
        SFcfg::search_column_width,
        width / 2,
        1,
        height / 2,
        SFcfg::max_speciment_tilt,
        cfg.vertical_pixels_per_mm_reference * SFcfg::specimen_thickness,
        SFcfg::mininum_number_of_agreeing_guesses,
        SFcfg::maximum_number_of_disagreeing_guesses};

    Frame* diagnostic_overlay = nullptr;
    #if SF_DRAW
    diagnostic_overlay = &overlay_frame;
    #endif

    std::optional<SpecimenPositionInfo> best_guess =
        processed_frame.find_specimen(search_parameters, diagnostic_overlay);

    if(!best_guess)
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

    return ProcessResult::DONE;
}
