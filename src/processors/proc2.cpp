#include "processors/proc2.hpp"
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

Proc2::Proc2(Config cfg) : FrameProcessor(cfg), output_file("data/proc2out.csv") {
    
}

#define OVERLAYSCALING 1
#define RAWSCALING 16

void Proc2::on_setup(unsigned framenum) {
    frame.reset(width, height, 0);
    procframe.assign(width*height,0);
    img_saver.reset(new ImageSaver(
    [this] (int ix) { 
        int gray = std::clamp((int)std::round(procframe[ix] * RAWSCALING + 128),0,255);
        float alpha = std::clamp(frame[ix] * OVERLAYSCALING,0.0F,1.0F);
        unsigned int red = std::round(alpha * 255 + (1 - alpha) * gray);
        unsigned int green_blue = std::round((1 - alpha) * gray);

        return red << 24U | green_blue << 16U | green_blue << 8U | 0xFF;
    },
    width,height,"proc_images/"
    ));

    // [R] Anchor time-dependent initialization to this processor's first frame.
    DIC_init(framenum * cfg.processing_interval);
}

void Proc2::on_termination(unsigned) {
    output_file.close();
}


void Proc2::procFrame(){
    std::fill(procframe.begin(),procframe.end(),0);
    //gaussian blur
    //kills hot pixels
    BasicFrame<std::int16_t> rawframe = get_raw_frame();
    
    [[maybe_unused]]int c = 0;
    for(int j = 1; j < height - 1; j++){
        const int offset = j * width;
        for(int i = 1; i < width - 1; i++){
            const int ix = offset + i;
            float avg = (float)(abs(rawframe[ix - 1]) + abs(rawframe[ix + 1]) + abs(rawframe[ix - width]) +abs(rawframe[ix + width]))/4;

            if(abs(rawframe[ix]) > 5 && abs(rawframe[ix]) > 8 * avg){
                rawframe[ix] = rawframe[ix] > 0 ? std::round(avg) : -std::round(avg);
                c++;
            }
        }
    }        
    //std::cout << c << std::endl;

    for(int j = 1; j < height - 1; j++){
        const int offset = j * width;
        for(int i = 1; i < width - 1; i++){
            const int ix = offset + i;
            procPS(i,j) = ( 
                12.f * rawframe[ix] 
                + 2.f * ( rawframe[ix+1] + rawframe[ix-1] + rawframe[ix+width] + rawframe[ix-width])
                + 1.f * ( rawframe[ix+width+1] + rawframe[ix+width-1] + rawframe[ix-width-1] + rawframe[ix-width+1])
                ) / 24.f;
        }
    }
}           

/*
    return the error between the square (x,y,w,h) in current frame and the square (x0,y0,w,h) in the reference frame
*/
float Proc2::correlate(int x, int y, DicBlock block){
    if( x + block.w > width || block.x0 + block.w > width || y + block.h > height || block.y0 + block.h > height){
        std::cout << "[Proc2/correlate] invalid call " << x << " " << y << ", block: " << block.x0 << " " << block.y0 << " " << block.w << " " << block.h << std::endl;
        return INFINITY;  
    }
    
    float R2 = 0;
    for(int i = 0; i < block.w; i++)
        for(int j = 0; j < block.h; j++){
            const float v = procPS(x+i,y+j) - refPS(block.x0+i,block.y0+j); 
            R2 += v * v;
    
        }
        
    return R2;
}

float Proc2::correlate(float x, float y, DicBlock block){
    float R2 = 0;
    float tx = x - std::floor(x), ty = y - std::floor(y);

    for(int i = 0; i < block.w;i++)
        for(int j = 0; j < block.h; j++){
            int xi = std::floor(x+i), yi = std::floor(y+j);
            float bl = procPS(xi,yi), br = procPS(xi+1,yi), tl = procPS(xi,yi+1),tr =procPS(xi+1,yi+1);
            float v = (bl * (1-tx) + br * tx)  * (1-ty) + (tl * (1-tx) + tr * tx) * ty;
            v -= refPS(block.x0+i,block.y0+j);
            R2 += v * v;
        }
    return R2;
}

void Proc2::DIC_init(float t){
    procFrame();
    ref_raw_frame = procframe;
    ref_frame_last_save = t;

    SpecimenPositionInfo pos = shared_data().specimen_position.value();

    for( int i = 0; i < width; i++)
        frame(i,std::round(pos.top.eval(i))) = 1;

    for( int i = 100; i + 25 < width; i += 25){
        blocks.push_back(DicBlock{
            i,(int)pos.top.eval(i)+1,25,30
        });
    }

    /*
    for(int i = 0; i < Proc2cfg::blocks_n_x; i++)
        for(int j = 0; j < Proc2cfg::blocks_n_y; j++)
            //blocks.push_back( {Proc2cfg::blocks_x0 + i * Proc2cfg::blocks_spacing_x,Proc2cfg::blocks_y0 + j * Proc2cfg::blocks_spacing_y,Proc2cfg::blocks_w,Proc2cfg::blocks_h });
            blocks.push_back( {
                width - Proc2cfg::blocks_dist_from_border - Proc2cfg::blocks_spacing_x * (Proc2cfg::blocks_n_x - 1) - Proc2cfg::blocks_w + i * Proc2cfg::blocks_spacing_x,
                Proc2cfg::blocks_y0 + j * Proc2cfg::blocks_spacing_y,
                Proc2cfg::blocks_w,Proc2cfg::blocks_h });
    */
}

/*
    maxoffset: guesstimation of maximum vertical movement of the blocks (DIC is very expensive)
    tracks the blocks' vertical motion using DIC and updates their position
    returns average y position of the blocks (!y not midline!)
*/
float Proc2::update_blocks_position(int maxoffset){
    float y_tot = 0;
    for(DicBlock& block : blocks) {
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
        y_tot += miny;
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
float Proc2::update_DIC_refereces(float blocks_y0){
    ref_raw_frame = procframe;
        
    //no blocks
    if(blocks.size() == 0)
        return -1.;
    //the first few blocks are in the part of the speciment where the outline is not a line
    if(blocks.size() < Proc2cfg::specimen_autocenter_min_blocks ){
        for(DicBlock& b : blocks)
            b.y0 = b.y;
        return -1.;
    }

    //now we have to find the top and bottom lines
    const int x0 = blocks[0].x, x1 = blocks.back().x + blocks.back().w;
    const int black_y0 = std::max((int)std::round(blocks_y0) - Proc2cfg::specimen_outline_search_interval / 2,0), 
              white_y0 = (int)std::round(blocks_y0) + blocks[0].h - Proc2cfg::specimen_outline_search_interval / 2;

    float black_line = 0, white_line = 0;

    for(int x = x0; x < x1; x++){
        float black_topscore = 0, black_topy = black_y0 + Proc2cfg::specimen_outline_search_interval;
        float white_topscore = 0, white_topy = white_y0;

        float black_score = -(procPS(x, black_y0 - 2) + procPS(x, black_y0 - 1) + procPS(x, black_y0));
        float white_score =  (procPS(x, white_y0 - 2) + procPS(x, white_y0 - 1) + procPS(x, white_y0));

        for(int dy = 0; dy < Proc2cfg::specimen_outline_search_interval && white_y0 + dy < height; dy++){
            black_score -= procPS(x,black_y0+dy+1);
            if(black_score > black_topscore) { black_topscore = black_score; black_topy = black_y0 + dy; }
            black_score += procPS(x,black_y0+dy-2);

            white_score += procPS(x,white_y0+dy+1);
            if(white_score > white_topscore) { white_topscore = white_score; white_topy = white_y0 + dy; }
            white_score -= procPS(x,white_y0+dy-2);
        }

        black_line += black_topy;
        white_line += white_topy;
    }

    black_line /= (float)(x1-x0);
    white_line /= (float)(x1-x0);

    #ifdef PROC2_DRAW
    for(int x = x0; x < x1; x++){
        pix(x,std::round(black_line)) = 1;
        pix(x,std::round(white_line)) = 1;
    }
    #endif

    const int new_y0 = std::round(black_line), new_h = std::round(white_line-black_line);
    //first block must not be center and allowed to drift with the crack
    blocks[0].y0 = blocks[0].y + (new_y0 - blocks[1].y);
    blocks[0].h = new_h;
    //other blocks are centered to specimen
    for( size_t i = 1; i < blocks.size(); i++){
        blocks[i].y0 = new_y0;
        blocks[i].h = new_h;
    }

    return (white_line + black_line) / 2;
}

float Proc2::find_specimen_body(float t){
    if( blocks.size() == 0)
        return -1.;
    
    float current_avg = 0;
    for(DicBlock b : blocks) current_avg += b.y0;
    current_avg /= blocks.size();

    //[HC] 3
    const int maxoffset = 3 + std::ceil(Proc2cfg::assumed_max_sinkrate * (t-ref_frame_last_save));

    // [HC] how far the specimen might have gone
    float new_avg = update_blocks_position(maxoffset);
    
    // [HC] drop the first block if it is out of line
    // [Note] a lone block is never gonna get dropped, we can still assume blocks non-empty
    // [Note] da capire come fare meglio, finché la storia rimane quasi statica sta roba funziona
    if(abs(blocks[0].y - new_avg) > 5 ){
        //removes first block from new average and then removes the first block
        new_avg = (new_avg * blocks.size() - blocks[0].y ) / (blocks.size() - 1);
        blocks.erase(blocks.begin());
    }

    // blocks (excluded the 1st one) get realigned
    for(size_t i = 1; i < blocks.size(); i++)
        blocks[i].y = new_avg;
 
    
    midline = new_avg + (float)blocks[0].h / 2;
    //if specimen has moved enough we update DIC refences
    if( new_avg - current_avg > Proc2cfg::max_displacement_before_DIC_ref_update){
        ref_frame_last_save = t;
        float val = update_DIC_refereces(new_avg);
        if ( val > 0 )
            midline = val;

        // 10 -> update every 100s
        //std::cout << "updated DIC ref t:" << t << std::endl;
    }

    //TODO: finish

    output_file << t << "," << midline << "\n";
    return midline;
}

#define L 10
#define C 2

float Proc2::find_crack(){
    if(blocks.size() == 0)
        return 0.;

    //now we have to find the top and bottom lines
    const int x0 = blocks[0].x, x1 = x0 + 300;
    const int y0 = std::round(midline) - L;

    const int scores_width = x1 - x0;
    size_t vecsize = scores_width*(2*L+1);
    std::vector<float> scores(vecsize);

    for(int dy = 0; dy < 2*L+1; dy++){
        int offset = dy * scores_width;
        int y = y0 + dy;

        float runningscore = 0;
        for(int x = x0 - C; x < x0 + C; x++)
            runningscore += procPS(x,y);

        for(int dx = 0; dx < scores_width; dx++){
            runningscore += procPS(x0 + dx + C,y);
            scores[offset + dx] = runningscore;
            runningscore -= procPS(x0 +dx - C,y);
        }
    }

    for(int x = x0; x < x1 && x + C < width; x++){
        float highscore = 0, highy = y0;
        float lowscore = 0, lowy = y0;

        for(int dy = 1; dy < 2 * L; dy++){
            #define scoreof(x,y) scores[scores_width * (y) + (x)]
            float score = scoreof(x-x0,dy+1) + scoreof(x-x0,dy) +scoreof(x-x0,dy-1);

            if(score > highscore){
                highscore = score;
                highy = y0+dy;
            }
            if(score < lowscore){
                lowscore = score;
                lowy = y0 +dy;
            }
        }

        pix(x,std::round(midline)) = 1;
        pix(x,highy) = 1;
        pix(x,lowy) = 1;
    }

    return 0;
}

ProcessResult Proc2::process_frame(unsigned framenum){
    float t = framenum * cfg.processing_interval;
    std::fill(frame.begin(), frame.end(), 0);
    //load the first frame as reference
    //this assumes that processing start is high enough that a fully formed pseudoframe is already formed
    
    procFrame();

    static float last_specimen_body_search_t = 0;
    if(t- last_specimen_body_search_t > Proc2cfg::specimen_body_search_interval){
    
        find_specimen_body(t);
        last_specimen_body_search_t = t;
    }

    find_crack();
    


    #ifdef PROC2_DRAW
    float a = .3 + .2 * rand() / RAND_MAX;
    for(DicBlock b : blocks)
        for(int i = 0; i < b.w; i++)
            for(int j = 0; j < b.h; j++)
                pix(b.x +i,b.y+j) += a;
    #endif
    return ProcessResult::OK;
}
