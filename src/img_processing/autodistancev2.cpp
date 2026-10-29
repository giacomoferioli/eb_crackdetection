#include "processors/autodistancev2.hpp"
#include "img_processing/frame.hpp"
#include "processing/processing.hpp"
#include "utils/best_n.hpp"
#include <cmath>
#include <cstddef>
#include <type_traits>
#include <vector>


AutoDistancev2::AutoDistancev2(Config cfg) : FrameProcessor(cfg), output_file("data/autodistout.csv") {

}

void AutoDistancev2::on_setup(unsigned){
    img_saver = std::make_unique<ImageSaver>(
        [this](int ix) {
            const int gray = std::clamp(
                static_cast<int>(std::lround(processed_frame[ix] * 16 + 128.0F)),
                0,
                255);
            const float alpha = std::clamp(overlay_frame[ix], 0.0F, 1.0F);
            const unsigned red = static_cast<unsigned>(std::lround(alpha * 255.0F + (1.0F - alpha) * gray));
            const unsigned green_blue = static_cast<unsigned>(std::lround((1.0F - alpha) * gray));
            return red << 24U | green_blue << 16U | green_blue << 8U | 0xFFU;
        },
        width,
        height,
        "proc_images/");
}

void AutoDistancev2::on_termination(unsigned){
    output_file.close();
}

#define Y0 134
#define CAMERA_H_FOV 0.722
#define CAMERA_H_RES 1280


ProcessResult AutoDistancev2::process_frame(unsigned int framenum){
    constexpr float approx_ppmm = 12;
    constexpr float approx_dash_length = 20;
    //has to be a lowball, if there are less that this number this code won't work
    constexpr float cm_visibles = 5;

    overlay_frame.reset(width, height,0);
    processed_frame = get_raw_frame();
    processed_frame.kill_hot_pixels(8,6);
    std::vector<VSegment> segments = processed_frame.find_vsegments(approx_dash_length);

    std::vector<bool> selected(segments.size());
    
    struct Row {
        std::vector<Point> points;
        float meany;
    };
    BestN<Entry<Row, size_t>> rows(4);

    //searches for the y coordinates of the rows containing the dashes
    for(size_t ix = 0; ix < segments.size(); ix++){
        if(selected[ix])
            continue;
        const Point& p = segments[ix];
        std::vector<size_t> selected_points_ix;
        Row candidate = { {p},p.y};
        for( size_t j = ix + 1; j < segments.size(); j++){
            if( selected[j])
                continue;
            const Point& p2 = segments[j];
            if( std::abs(p2.y - candidate.meany) < approx_dash_length ){
                candidate.meany = candidate.meany * candidate.points.size() + p2.y;
                candidate.points.push_back(p2);
                selected_points_ix.push_back(j);
                candidate.meany /= candidate.points.size();                
            }
        }

        //we
        if(candidate.points.size() > cm_visibles * 10 * 0.7){
            for( size_t j : selected_points_ix)
                selected[j] = true;
            
            rows.insert({candidate,candidate.points.size()});
        }
    }

    /*for codex : put you code inside this block*/





    /* end block for codex */
    return ProcessResult::OK;
}
