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
int Proc1::hlinefinder(int xc,bool type,int h, int minimum_length,bool first){
    int topy = -1, topscore = HLINEFINDER_MINIMUM_SCORE_MUL * HLINEFINDER_SCORING_H_L * h, toptotscore = 0;
    
    auto calcscore = [this,type,h] (int x,int y) {
        int score = 0;
        for(int xs = x - HLINEFINDER_SCORING_H_L; xs <= x + HLINEFINDER_SCORING_H_L; xs++){
            for(int ys = y - (h-1)/2; ys <= y + h/2; ys ++)
                type ? score += PS(xs,ys) : score -= PS(xs,ys);
        }
        return score;
    };

    for(int y = h / 2; y < height - h; y++){
        //calculates score
        int score = calcscore(xc, y);
        frame[width * y + xc] = score;

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
            int running_score = score;
            double running_y = y;
            int length = 1;
            //look rigth
            int totscore = score;
            for( int x = xc; running_score > score / 2 && x < xc + minimum_length && x < width - HLINEFINDER_SCORING_H_L; x++, length++){
                running_score = calcscore(x,(int)std::round(running_y));
                int running_score_up = calcscore(x,(int)std::round(running_y)+1),
                    running_score_down = calcscore(x,(int)std::round(running_y)-1);

                if( running_score_up > running_score){
                    running_score = running_score_up;
                    running_y += (1 - HLINEFINDER_RUNNING_Y_INERTIA);
                }

                if( running_score_down > running_score){
                    running_score = running_score_down;
                    running_y -= (1 - HLINEFINDER_RUNNING_Y_INERTIA);
                }
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

    int x0 = width / 8;
    
    double specimen_bottom = hlinefinder(x0, true,3);
    double specimen_top = hlinefinder(x0, false,1,500,true);
    
    //couldn't find specimen outline, drop frame
    if( specimen_bottom == -1 || specimen_top == -1)
        return;

    int n = 0;
    double totheight = 0;
    //double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;

    int s,sup,sdown;
    #define PROC1_SEARCH_H_L 6
    #define PROC1_INERTIA 0.7
    int specimen_bottom_score = score(x0, specimen_bottom, PROC1_SEARCH_H_L),
        specimen_top_score = score(x0, specimen_top, PROC1_SEARCH_H_L);
    double running_score_top = specimen_top_score, running_score_bottom = specimen_bottom_score;
    for(int x = x0; x < width - PROC1_SEARCH_H_L; x++ ){
        //top (black)
        s = score(x, std::round(specimen_top), PROC1_SEARCH_H_L);
        sup = score(x, std::round(specimen_top+1), PROC1_SEARCH_H_L);
        sdown = score(x, std::round(specimen_top-1), PROC1_SEARCH_H_L);

        if( sup < s ){ specimen_top += (1 - PROC1_INERTIA); s = sup; }
        if( sdown < s ){ specimen_top -= (1 - PROC1_INERTIA); s = sdown; }

        running_score_top = running_score_top * PROC1_INERTIA + (1 - PROC1_INERTIA) * s;
        if( (int)running_score_top > specimen_top_score / 4) break;

        //bottom (white)
        s = score(x, std::round(specimen_bottom), PROC1_SEARCH_H_L);
        sup = score(x, std::round(specimen_bottom+1), PROC1_SEARCH_H_L);
        sdown = score(x, std::round(specimen_bottom-1), PROC1_SEARCH_H_L);

        if( sup > s ){ specimen_bottom += (1 - PROC1_INERTIA); s = sup; }
        if( sdown > s ){ specimen_bottom -= (1 - PROC1_INERTIA); s = sdown; }

        running_score_bottom = running_score_bottom * PROC1_INERTIA + (1 - PROC1_INERTIA) * s;
        if( (int)running_score_bottom < specimen_bottom_score / 4) break;

        totheight += (specimen_top+specimen_bottom)/2;
        n++;
        //pix(x,std::round((specimen_top+specimen_bottom)/2)) = 16;        
    }

    double specimen_middle = totheight / n;

    static double original_specimen_middle = 0;
    if(original_specimen_middle == 0 && specimen_middle != NAN){
        original_specimen_middle = specimen_middle;
        return;
    }

    #define SINKRATE 0.0885// px/sec
    for(int x = 0; x < width; x++){
        for(int y = 25; y < 80; y++ ){
            
            //float offset = (float)framenum * cfg.processing_interval / 1e6 * SINKRATE;
            float offset = specimen_middle - original_specimen_middle;

            float newy_f = y - offset;

            int newy = (int) newy_f;
            float t = newy_f - std::floor(newy_f);

            pix(x,newy) += (1-t) * PS(x,y);
            pix(x,newy) += t * PS(x,y);
        }
    }
    
    f_displacement << framenum * cfg.processing_interval / 1e6 << "," << specimen_middle << "\n";

}