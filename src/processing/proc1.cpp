#include "processing/proc1.hpp"
#include "image_saver.hpp"

#include <algorithm>
#include <cmath>
#include <ios>
#include <iostream> 
#include <vector>

Proc1::Proc1(EventSource& source, Config cfg) : Processing(source,cfg),f_displacement("displ.csv") {
    
}

#define OVERLAYSCALING 2
#define RAWSCALING 32

void Proc1::on_setup() {
    img_saver.reset(new ImageSaver(
    [this] (int ix) { 
        int gray = std::clamp(128 + PS(ix) * RAWSCALING,0,255);
        int red = std::clamp( OVERLAYSCALING * frame[ix],0,255 );
        int blue = std::clamp( OVERLAYSCALING * -frame[ix],0,255 );
        int alpha = std::max(red,blue);

        return 0xFF 
        | (red + gray * (256-alpha) / 256) << 24
        | (gray * (256-alpha)/256) << 16
        | (blue + gray * (256-alpha) / 256) << 8;
    },
    width,height,"proc_images/"
    ));

    matrix = new int[9];
    matrix[0] = matrix[1] = matrix[2] = -1;
    matrix[3] = matrix[4] = matrix[5] = 1;
    matrix[6] = matrix[7] = matrix[8] = -1;
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
                
                frame[(std::round(running_y)+1) * width + x] = running_score_up;
                frame[(std::round(running_y)) * width + x] = running_score;
                frame[(std::round(running_y)-1) * width + x] = running_score_down;

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

void Proc1::process_frame(){
    static int framenum = 0;
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
        pix(x,std::round(specimen_top)) = s = score(x, std::round(specimen_top), PROC1_SEARCH_H_L);
        pix(x,std::round(specimen_top+1)) = sup = score(x, std::round(specimen_top+1), PROC1_SEARCH_H_L);
        pix(x,std::round(specimen_top-1)) = sdown = score(x, std::round(specimen_top-1), PROC1_SEARCH_H_L);

        if( sup < s ){ specimen_top += (1 - PROC1_INERTIA); s = sup; }
        if( sdown < s ){ specimen_top -= (1 - PROC1_INERTIA); s = sdown; }

        running_score_top = running_score_top * PROC1_INERTIA + (1 - PROC1_INERTIA) * s;
        if( (int)running_score_top > specimen_top_score / 4) break;

        //bottom (white)
        pix(x,std::round(specimen_bottom)) = s = score(x, std::round(specimen_bottom), PROC1_SEARCH_H_L);
        pix(x,std::round(specimen_bottom+1)) = sup = score(x, std::round(specimen_bottom+1), PROC1_SEARCH_H_L);
        pix(x,std::round(specimen_bottom-1)) = sdown = score(x, std::round(specimen_bottom-1), PROC1_SEARCH_H_L);

        if( sup > s ){ specimen_bottom += (1 - PROC1_INERTIA); s = sup; }
        if( sdown > s ){ specimen_bottom -= (1 - PROC1_INERTIA); s = sdown; }

        running_score_bottom = running_score_bottom * PROC1_INERTIA + (1 - PROC1_INERTIA) * s;
        if( (int)running_score_bottom < specimen_bottom_score / 4) break;

        totheight += (specimen_top+specimen_bottom)/2;
        n++;
        //pix(x,std::round((specimen_top+specimen_bottom)/2)) = 16;        
    }

    double specimen_middle = totheight / n;

    //now we look on the midline to find the pixels
    int ym = std::round(specimen_middle);

    int ybl = hlinefinder(x0,false,3);

    pix(x0, ybl) = -255;

    f_displacement << framenum * cfg.processing_interval / 1e6 << "," << specimen_middle << "\n";

    if((++framenum & 0xFF) == 0) std::cout << "\r" << (double)framenum * cfg.processing_interval / 1e6 << "      " << std::flush;
}