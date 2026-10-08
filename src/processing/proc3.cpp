#include "processing/proc3.hpp"
#include "image_saver.hpp"

#include <algorithm>
#include <cmath>
#include <ios>
#include <iostream> 
#include <vector>

Proc3::Proc3(EventSource& source, Config cfg) : Processing(source,cfg)/*,f_displacement("data/displ.csv")*/ {
    
}

#define OVERLAYSCALING 128

void Proc3::on_setup() {
    frame.assign(width*height,0);
    img_saver.reset(new ImageSaver(
    [this] (int ix) { 
        int gray = std::clamp((int)std::round(frame[ix] * OVERLAYSCALING),0,255);

        return gray << 24U | gray << 16U | gray << 8U| 0xFF;
    },
    width,height,"proc_images/"
    ));
}

void Proc3::on_termination() {
    //f_displacement.close();
}
//do not call on frontier points
inline float Proc3::hlinescore(int x0, int y0, int l) const{
    float score = 0;
    for(int x = x0 - l; x <= x0+l; x++){
        score += abs(PS(x,y0-1)) + abs(PS(x,y0)) + abs(PS(x,y0 + 1));
    }
    return score / (2 * l + 1) / 3;
}

void Proc3::process_frame(unsigned framenum){
    frame.assign(height * width, 0);
    inter_frame.assign(height * width, 0);

    #define L 4
    
    float m = 1. / 3. / (2 * L + 1); 
    for(int y = 0; y < height; y++){
        double running_score = 0;
        for(int i = 0; i < 2 * L; i++)
            running_score += abs(std::clamp((int)PS(i,y),-3,3)) * m;

        for(int x = L; x < width - L; x++){
            running_score += abs(std::clamp((int)PS(x+L,y),-3,3)) * m;
            inter_pix(x,y) = running_score;
            running_score -= abs(std::clamp((int)PS(x-L,y),-3,3)) * m;
        }
    }

    for(int x = 0; x < width; x++){
        double running_score = 0;
        for(int i = 0; i < 2 * L; i++)
            running_score += abs(std::clamp((int)PS(x,i),-3,3)) * m;

        for(int y = L; y < height - L; y++){
            running_score += abs(std::clamp((int)PS(x,y+L),-3,3)) * m;
            pix(x,y) = inter_pix(x, y) - inter_pix(x, y+1) - inter_pix(x, y-1) - running_score;
            running_score -= abs(std::clamp((int)PS(x,y-L),-3,3)) * m;
        }
    }

    
    return;

    for(int x = 1; x < width - 1; x++)
        for(int y = 2; y < height - 2; y++)
            pix(x,y) = -inter_pix(x, y+1) + 3 * inter_pix(x, y) - inter_pix(x, y-1);
            //pix(x,y) = -inter_pix(x, y+2) + 2 * inter_pix(x, y+1) + inter_pix(x, y) + 2 * inter_pix(x, y-1) - inter_pix(x, y-2);

    
    //f_displacement << framenum * cfg.processing_interval / 1e6 << "," << specimen_middle << "\n";
}