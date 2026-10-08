#include "processing/proc1.hpp"
#include "image_saver.hpp"

#include <algorithm>
#include <cmath>
#include <ios>
#include <iostream> 
#include <vector>

Proc1::Proc1(EventSource& source, Config cfg) : Processing(source,cfg),f_displacement("data/displ.csv") {
    
}

#define OVERLAYSCALING 2
#define RAWSCALING 16

void Proc1::on_setup() {
    frame.assign(width*height,0);
    img_saver.reset(new ImageSaver(
    [this] (int ix) { 
        int gray = std::clamp((int)std::round(frame[ix] * RAWSCALING + 128),0,255);

        return gray << 24U | gray << 16U | gray << 8U| 0xFF;
    },
    width,height,"proc_images/"
    ));
}

void Proc1::on_termination() {
    f_displacement.close();
}
//do not call on frontier points
inline int Proc1::score(int x0, int y0, int l) const{
    int score = 0;
    for(int x = x0; x <= x0+l; x++){
        score += PS(x,y0-1) + PS(x,y0) + PS(x,y0 + 1);
    }
    return score;
}
/* Searched for an hline, scans the column (x,-)
    tpye true -> looks for positive line; false -> looks for negative line
    
    this function is not safe near image borders, TODO: make it safe to detect lines near borders
*/
#define HLINEFINDER_SCORING_H_L 3
#define HLINEFINDER_RUNNING_Y_INERTIA 0.7
#define HLINEFINDER_MINIMUM_SCORE_MUL 2
int Proc1::hlinefinder(int xc,int y0,int y1,bool type,int h, int minimum_length,bool first){
    int topy = -1, topscore = HLINEFINDER_MINIMUM_SCORE_MUL * HLINEFINDER_SCORING_H_L * h, toptotscore = 0;
    
    auto calcscore = [this,type,h] (int x,int y) {
        int score = 0;
        for(int xs = x - HLINEFINDER_SCORING_H_L; xs <= x + HLINEFINDER_SCORING_H_L; xs++){
            for(int ys = y - (h-1)/2; ys <= y + h/2; ys ++)
                type ? score += PS(xs,ys) : score -= PS(xs,ys);
        }
        return score;
    };

    for(int y = std::max(h / 2,y0); y < std::min(height - h,y1); y++){
        //calculates score
        int score = calcscore(xc, y);
        //possible candidate
        if(score > topscore){
            //now we calculate length 

            //if we have a thick line we may trigger the if prematurely bc a point on the edge of the line has a score big enough
            //this for makes sure we have the possible line true score
            for(int dy = 0; dy < 5; dy++){
                int tmpscore = calcscore(xc, y + dy);
                if(tmpscore > score){
                    score = tmpscore;
                    y += dy;
                }
            }

            //score of points we are looking at
            int running_score = score, length = 1;
            double running_y = y;
            //look rigth
            int totscore = score;
            for( int x = xc; running_score > score / 2 && x < width - HLINEFINDER_SCORING_H_L; x++, length++){
                int round_y = std::round(running_y);
                running_score = calcscore(x,round_y);
                int running_score_up = calcscore(x,round_y+1),
                    running_score_down = calcscore(x,round_y-1);

                if( running_score_up > running_score){
                    running_score = running_score_up;
                    running_y += (1 - HLINEFINDER_RUNNING_Y_INERTIA);
                }

                if( running_score_down > running_score){
                    running_score = running_score_down;
                    running_y -= (1 - HLINEFINDER_RUNNING_Y_INERTIA);
                }
                pix(x,running_y) = 100;

                if(x > rightmost_x)
                    rightmost_x = x;
            }
            running_score = score;
            for( int x = xc; running_score > score / 2 && x > HLINEFINDER_SCORING_H_L; x--, length++){
                int round_y = std::round(running_y);
                running_score = calcscore(x,round_y);
                int running_score_up = calcscore(x,round_y+1),
                    running_score_down = calcscore(x,round_y-1);

                if( running_score_up > running_score){
                    running_score = running_score_up;
                    running_y += (1 - HLINEFINDER_RUNNING_Y_INERTIA);
                }

                if( running_score_down > running_score){
                    running_score = running_score_down;
                    running_y -= (1 - HLINEFINDER_RUNNING_Y_INERTIA);
                }
                pix(x,running_y) = 100;
            }
            
            if(length > minimum_length && totscore > toptotscore){
                topy = y;
                topscore = score;
                toptotscore = totscore;

                if (first)
                    return topy;
            }
        }
    } 

    return topy;
}

void Proc1::process_frame(unsigned framenum){
    frame.assign(height * width, 0);

    if(1e-6 * framenum * cfg.processing_interval < 27)
        return;

    static int cx = 350, cy = 190;

    for(int x = std::max(cx - 100,0);x < std::min(cx + 100,width);x++)
        for(int y = std::max(cy-20,0); y < std::min(cy+20,height); y++)
            pix(x,y) = PS(x,y);

    rightmost_x = 0;
    
    int y = hlinefinder(cx, cy-20, cy+20, false,2,40);
    if ( abs(y - cy) > 5 )
        return;

    cy = y;
    cx = cx * 0.8 + std::max(cx,rightmost_x - 50) * 0.2;

    
    //f_displacement << framenum * cfg.processing_interval / 1e6 << "," << specimen_middle << "\n";
}