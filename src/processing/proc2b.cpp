#include "processing/proc2b.hpp"
#include "image_saver.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream> 
#include <ostream>
#include <vector>

#define PROC2_DRAW

Proc2b::Proc2b(EventSource& source, Config cfg) : Processing(source,cfg), output_file("data/proc2out.csv") {
    
}

#define OVERLAYSCALING 1
#define RAWSCALING 16

void Proc2b::on_setup() {
    overlay_frame.reset(width, height, 0.0F);
    processed_frame.reset(width, height, 0.0F);
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
}

void Proc2b::on_termination() {
    output_file.close();
}


void Proc2b::procFrame(){
    processed_frame.fill(0.0F);
    //gaussian blur
    //kills hot pixels
    Frame raw_frame(width, height);
    const std::vector<std::int16_t>& raw_pixels = get_raw_frame();
    std::copy(raw_pixels.begin(), raw_pixels.end(), raw_frame.begin());
    
    int c = 0;
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
    
    for(int j = 1; j < height - 1; j++){
        for(int i = 1; i < width - 1; i++){
            processed_frame(i, j) = (
                12.f * raw_frame(i, j)
                + 2.f * (raw_frame(i + 1, j) + raw_frame(i - 1, j) + raw_frame(i, j + 1) + raw_frame(i, j - 1))
                + 1.f * (raw_frame(i + 1, j + 1) + raw_frame(i - 1, j + 1) + raw_frame(i - 1, j - 1) + raw_frame(i + 1, j - 1))
                ) / 24.f;
        }
    }
}           

/*
    return the error between the square (x,y,w,h) in current frame and the square (x0,y0,w,h) in the reference frame
*/
float Proc2b::correlate(int x, int y, Block block){
    if( x + block.w > width || block.x0 + block.w > width || y + block.h > height || block.y0 + block.h > height){
        std::cout << "[Proc2/correlate] invalid call " << x << " " << y << ", block: " << block.x0 << " " << block.y0 << " " << block.w << " " << block.h << std::endl;
        return INFINITY;  
    }
    
    float R2 = 0;
    for(int i = 0; i < block.w; i++)
        for(int j = 0; j < block.h; j++){
            const float v = processed_frame(x + i, y + j) - reference_frame(block.x0 + i, block.y0 + j);
            R2 += v * v;
    
        }
        
    return R2;
}

float Proc2b::correlate(float x, float y, Block block){
    float R2 = 0;
    float tx = x - std::floor(x), ty = y - std::floor(y);

    for(int i = 0; i < block.w;i++)
        for(int j = 0; j < block.h; j++){
            int xi = std::floor(x+i), yi = std::floor(y+j);
            float bl = processed_frame(xi, yi), br = processed_frame(xi + 1, yi);
            float tl = processed_frame(xi, yi + 1), tr = processed_frame(xi + 1, yi + 1);
            float v = (bl * (1-tx) + br * tx)  * (1-ty) + (tl * (1-tx) + tr * tx) * ty;
            v -= reference_frame(block.x0 + i, block.y0 + j);
            R2 += v * v;
        }
    return R2;
}

void Proc2b::init(float t){
    procFrame();
    reference_frame = processed_frame;
    ref_frame_last_save = t;

    for(int i = 0; i < Proc2cfg::blocks_n_x; i++)
        for(int j = 0; j < Proc2cfg::blocks_n_y; j++){
            //blocks.push_back( {Proc2cfg::blocks_x0 + i * Proc2cfg::blocks_spacing_x,Proc2cfg::blocks_y0 + j * Proc2cfg::blocks_spacing_y,Proc2cfg::blocks_w,Proc2cfg::blocks_h });
            blocks.push_back( {
                width - Proc2cfg::blocks_dist_from_border - Proc2cfg::blocks_w - i * Proc2cfg::blocks_spacing_x,
                Proc2cfg::blocks_y0 + j * Proc2cfg::blocks_spacing_y,
                Proc2cfg::blocks_w,Proc2cfg::blocks_h });
            }
}

/*
    maxoffset: guesstimation of maximum vertical movement of the blocks (DIC is very expensive)
    tracks the blocks' vertical motion using DIC and updates their position
    returns midline (avg of blocks center)
*/
float Proc2b::update_blocks_position(int maxoffset){
    float y_tot = 0;
    for(Block& block : blocks) {
        float minscore = 1e10;
        int miny = 0;
        for(int y = block.y0; y < block.y0 + maxoffset; y++){
            const float score = correlate(block.x0,y,block);

            if(score < minscore){
                minscore = score;
                miny = y; 
            }
        }

        block.y = miny;
        y_tot += miny + 0.5 * block.h;
    }

    return y_tot / blocks.size();
}

/* Updates the block DIC references
    notes: assumes the blocks (except the first) have the same y0,y,w,h
    TODOs: compensate horizontal movement of the specimen
    1. copies the actual proc frame to the reference frame
    2. if there are too few blocks it just updates the block references with the actual block position and returns
    3. uses a line finder algorithm to find specimen top (black) and bottom (white), searches in proximity of the blocks top and bottom
    4. using the specimen top and bottom it modifies block's references (y0 and h) so that they overlay the specimen
        - first block does not get centered, if the other blocks get offset by X pixels by the centering so is the first block
    retruns the specimen midline
*/
float Proc2b::update_DIC_refereces(float){
    reference_frame = processed_frame;
        
    //no blocks
    if(blocks.size() == 0)
        return -1.;
    //the first few blocks are in the part of the speciment where the outline is not a line
    if(blocks.size() < 2 * Proc2cfg::specimen_autocenter_min_blocks ){
        for(Block& b : blocks)
            b.y0 = b.y;
        return -1.;
    }

    //this uses the bottom and top line to move the blocks
    midline = 0;
    for(size_t i = 0; i < blocks.size() / 2; i++){
        Line blackline = processed_frame.find_hline(
            top_block(i).x, top_block(i).y - Proc2cfg::specimen_outline_search_interval / 2, 
            top_block(i).w, Proc2cfg::specimen_outline_search_interval,Line::Type::BLACK);
        int whiteline_y0 = std::round(( btm_block(i).bottom() + btm_block(i==0 ? 2 : i-1).bottom() + btm_block((i==blocks.size()/2-1) ? blocks.size()/2-3 : i+1).bottom() )/3.f);    
        Line whiteline = processed_frame.find_hline(
                btm_block(i).x,whiteline_y0 - Proc2cfg::specimen_outline_search_interval / 2,
                btm_block(i).w, Proc2cfg::specimen_outline_search_interval,Line::Type::WHITE);


        float blacky = blackline.eval(top_block(i).center_x());
        float whitey = whiteline.eval(btm_block(i).center_x());
        midline += ( blacky + whitey ) / 2;

        top_block(i).y = top_block(i).y0 = std::round(blacky);
        btm_block(i).y = btm_block(i).y0 = std::round(whitey) - btm_block(i).h;

        #ifdef PROC2_DRAW
        for(int x = top_block(i).x; x < top_block(i).x + top_block(i).w; x++){
            overlay_frame(x, std::round( blackline.eval(x) )) = 1;
            overlay_frame(x, std::round(whiteline.eval(x))) = 1;
        }
        #endif
    }

    //kills outliers
    for(size_t i = 1; i < blocks.size() / 2 - 1; i++){
        float expected = 0.5 * (btm_block(i-1).y0 + btm_block(i+1).y0);
        
        float R2 = std::pow(btm_block(i).y - expected,2);
        float neighboursR2 = (std::pow(btm_block(i-1).y - expected,2) + std::pow(btm_block(i+1).y - expected,2))/2;
        
        if ( std::abs(btm_block(i).y0 - expected) > 5 && R2 > 4 * neighboursR2 )
            btm_block(i).y0 = expected;
    }

    /* last block correction */ {
    size_t n = blocks.size() / 2 - 1;
    float expected = 2 * btm_block(n-1).y0 - btm_block(n-2).y0;
    if( std::abs(btm_block(n).y0 - expected) > 5)
        btm_block(n).y0 = expected;
    }


    float specimen_thickness = 0;
    // merges blocks tills it finds a widening
    for(int i = 0; i < uncracked_specimen - 2; i++ ){
        specimen_thickness += btm_block(i).bottom() - top_block(i).y;
    } 
    specimen_thickness /= (uncracked_specimen - 2);

    for(size_t i = 0; i < blocks.size() / 2; i++){
        if( btm_block(i).bottom() - top_block(i).y > specimen_thickness + 2){
            uncracked_specimen = i-1;
            break;
        }
        
        int thickness = specimen_thickness / 2;
        top_block(i).h = thickness;
        btm_block(i).y0 -= (thickness - btm_block(i).h);
        btm_block(i).h = thickness;
    }
 
    midline /= 0.5 * blocks.size();

    return midline;
}

float Proc2b::find_specimen_body(float t){
    if( blocks.size() == 0)
        return -1.;
    
    float current_avg = 0;
    for(Block b : blocks ) current_avg += b.y0 + 0.5 * b.h;
    current_avg /= blocks.size();
    
    //[HC] 3
    const int maxoffset = 3 + std::ceil(Proc2cfg::assumed_max_sinkrate * (t-ref_frame_last_save));

    // [HC] how far the specimen might have gone
    float new_avg = update_blocks_position(maxoffset);
    
    midline = new_avg;
    //if specimen has moved enough we update DIC refences
    if( new_avg - current_avg > Proc2cfg::max_displacement_before_DIC_ref_update){
        ref_frame_last_save = t;
        update_DIC_refereces(new_avg);
    }

    //TODO: finish

    //output_file << t << "," << midline << "\n";
    return midline;
}

#define L 10
#define C 2

float Proc2b::find_crack(){
    constexpr int sample_count = 5;

    if(uncracked_specimen < 0)
        return -1.0F;

    const size_t first_block = static_cast<size_t>(uncracked_specimen) + 1;
    const size_t block_count = blocks.size() / 2;
    if(first_block + sample_count > block_count)
        return -1.0F;

    double distances[sample_count];
    double block_x[sample_count];

    for(int sample = 0; sample < sample_count; sample++){
        const size_t i = first_block + sample;
        Block& top = top_block(i);
        Block& bottom = btm_block(i);

        Line top_bottom_line = processed_frame.find_hline(
            top.x,
            top.bottom() - Proc2cfg::specimen_outline_search_interval / 2,
            top.w,
            Proc2cfg::specimen_outline_search_interval,
            Line::Type::WHITE);

        Line bottom_top_line = processed_frame.find_hline(
            bottom.x,
            bottom.y - Proc2cfg::specimen_outline_search_interval / 2,
            bottom.w,
            Proc2cfg::specimen_outline_search_interval,
            Line::Type::BLACK);

        block_x[sample] = 0.5 * (top.center_x() + bottom.center_x());
        distances[sample] = bottom_top_line.eval(bottom.center_x()) - top_bottom_line.eval(top.center_x());

        for(int i = top.x; i < top.x + top.w; i++){
            overlay_frame(i,top_bottom_line.eval(i)) = 1;
            overlay_frame(i,bottom_top_line.eval(i)) = 1;
        }
    }

    // Least-squares fit of distance = m*u + q, where u is the block offset
    // 0..4. Using block offsets keeps the calculation well-scaled.
    double sum_u = 0.0;
    double sum_distance = 0.0;
    double sum_u2 = 0.0;
    double sum_u_distance = 0.0;

    for(int sample = 0; sample < sample_count; sample++){
        const double u = sample;
        sum_u += u;
        sum_distance += distances[sample];
        sum_u2 += u * u;
        sum_u_distance += u * distances[sample];
    }

    const double denominator = sample_count * sum_u2 - sum_u * sum_u;
    if(std::abs(denominator) < 1e-12)
        return -1.0F;

    const double slope =
        (sample_count * sum_u_distance - sum_u * sum_distance) /
        denominator;
    if(std::abs(slope) < 1e-12)
        return -1.0F;

    const double intercept = (sum_distance - slope * sum_u) / sample_count;
    const double root = -intercept / slope;

    const double pixels_per_block = (block_x[sample_count - 1] - block_x[0]) / (sample_count - 1);
    const int crack_x = std::lround(block_x[0] + root * pixels_per_block);
    if(crack_x < 0 || crack_x >= width)
        return -1.0F;

    for(int y = 0; y < height; y++)
        overlay_frame(crack_x, y) = 1.0F;

    return static_cast<float>(crack_x);
}

void Proc2b::process_frame(unsigned framenum){
    float t = framenum * cfg.processing_interval;
    overlay_frame.fill(0.0F);
    //load the first frame as reference
    //this assumes that analysis start is high enough that a fully formed pseudoframe is already formed
    if(reference_frame.empty()) {
        init(t);
        return;
    }
    
    procFrame();

    static float last_specimen_body_search_t = -Proc2cfg::specimen_body_search_interval;
    if(t- last_specimen_body_search_t >= Proc2cfg::specimen_body_search_interval){
    
        find_specimen_body(t);
        last_specimen_body_search_t = t;

        
    }

    double crack = find_crack();
    output_file << t << "," << crack << "\n";


    #ifdef PROC2_DRAW
    
    srand(0);
    for( Block b : blocks){
        float a = .3 + .2 * rand() / RAND_MAX;
        for(int i = 0; i < b.w; i++)
            for(int j = 0; j < b.h; j++)    
                overlay_frame(b.x + i, b.y + j) += a;
        }

    if(uncracked_specimen >= 0)
        for(int j = 0; j < height; j++)
            overlay_frame(top_block(uncracked_specimen).x,j) = 0.3;
    #endif
}
