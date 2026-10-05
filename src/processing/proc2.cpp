#include "processing/proc2.hpp"
#include "image_saver.hpp"

#include <algorithm>
#include <cmath>
#include <iostream> 
#include <limits>
#include <vector>

Proc2::Proc2(EventSource& source, Config cfg) : Processing(source,cfg),f_displacement("data/displ2.csv") {
    
}

#define OVERLAYSCALING 2
#define RAWSCALING 16

void Proc2::on_setup() {
    frame.assign(width*height,0);
    img_saver.reset(new ImageSaver(
    [this] (int ix) { 
        int gray = std::clamp((int)std::round(frame[ix] * RAWSCALING + 128),0,255);

        return gray << 24U | gray << 16U | gray << 8U| 0xFF;
    },
    width,height,"proc_images/"
    ));
}

void Proc2::on_termination() {
    //f_displacement.close();
}

/*
    return the error between the square (x,y,w,h) in current frame and the square (x0,y0,w,h) in the reference frame
*/
double Proc2::correlate(int x, int y, int width, int height, int x0,int y0){
    double R2 = 0;
    for(int i = 0; i < width; i++)
        for(int j = 0; j < height; j++)
            R2 += pow(PS(x+i,y+j) - refPS(x0+i,y0+j) ,2);

    return R2;
}

double Proc2::correlate(double x, double y, int width, int height, int x0,int y0){
    double R2 = 0;
    double tx = x - std::floor(x), ty = y - std::floor(y);

    for(int i = 0; i < width; i++)
        for(int j = 0; j < height; j++){
            int xi = std::floor(x+i), yi = std::floor(y+j);
            double bl = PS(xi,yi), br = PS(xi+1,yi), tl = PS(xi,yi+1),tr =PS(xi+1,yi+1);
            double v = (bl * (1-tx) + br * tx)  * (1-ty) + (tl * (1-tx) + tr * tx) * ty;
            R2 += pow(v - refPS(x0+i,y0+j) ,2);
        }


    return R2;
}

#define original_specimen_middle 40
#define H 40
#define W 60

#define T0 15'000'000
#define T 10'000'000
#define MAXSINKRATE 0.1


void Proc2::process_frame(unsigned framenum){
    std::fill(frame.begin(),frame.end(),0);

    for(int i = 1; i < width - 1; i++)
        for(int j = 1; j < height - 1; j++)
            pix(i,j) = PS(i,j) - PS(i-1,j) - PS(i+1,j) - PS(i,j-1) - PS(i,j+1);

    return;
    static int lastframesave = 0;
    static double lastoffset = 0;   

    if( ref_raw_frame.size() == 0){
        if(framenum * cfg.processing_interval > T0){
            ref_raw_frame = get_raw_frame();
            lastframesave = framenum;
        }
        else
            return;
    }



    int maxoffset = 3 + std::ceil(MAXSINKRATE * (framenum - lastframesave) * cfg.processing_interval / 1e6);

    int y = original_specimen_middle - H / 2;
    
    double minscore = 1e10;
    int offset_i = 0;
    int x = 560;// 450 + 1.5e-6 * framenum * cfg.processing_interval;
    for(int dy = 0; dy < maxoffset; dy++){
        double score = correlate(x, y+dy, W,H,x,y);

        if(score < minscore){
            minscore = score;
            offset_i = dy; 
        }
    }

    double offset = offset_i;
    for(double offset_d = offset_i - 0.9; offset_d < offset_i+0.999; offset_d += 0.1){
        double score = correlate((double)x, y +offset_d, W,H,x,y);

        if(score < minscore){
            minscore = score;
            offset = offset_d; 
        }
    }

    offset += lastoffset;

    if( (framenum & 0b111) == 0)
        f_displacement << framenum * cfg.processing_interval / 1e6 << "," << offset << "\n";

    for(int x = 0; x < width; x++){
        for(int y = 0; y < 100; y++ ){
            
            //float offset = (float)framenum * cfg.processing_interval / 1e6 * SINKRATE;
            
            double t = offset - std::floor(offset);
            int y0 = y + (int)offset;
            pix(x,y) = PS(x,y0) * (1-t) + PS(x,y0+1) * t;
        }
    }


    if((framenum-lastframesave) * cfg.processing_interval > T){
        ref_raw_frame = get_raw_frame();
        lastframesave = framenum;
        lastoffset = offset;
        
    }
        
    
}