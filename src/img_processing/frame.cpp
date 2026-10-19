#include "frame.hpp"
#include "datatypes.hpp"

#include <iostream>
#include <limits>

//decentemente controllato
Line Frame::find_hline(int x0, int y0, int w, int h,Line::Type type) const{
    if( x0 < FrameCfg::line_search_semi_interval || x0 + w > width_ - FrameCfg::line_search_semi_interval ||
        y0 < 1 || y0 + h > height_ - 1 ||
        w < 3 || h < 3){
            std::cout << "find_hline bad input" << std::endl;
            return {{0,0},0,Line::ANY};
        }
    
    Frame scores(w,h);

    const int l = FrameCfg::line_search_semi_interval;


    for(int dy = 0; dy < h; dy++){
        float running_score = 0;

        for(int x = x0 - l; x < x0 + l; x++)
            running_score += (*this)(x,y0+dy);

        for(int dx = 0; dx < w; dx++){
            running_score += (*this)(x0+dx+l,y0+dy);
            scores(dx,dy) = running_score;
            running_score -= (*this)(x0+dx-l,y0+dy);
        }
    }

    float avg_y_rel = 0;
    //reuses scores(dx,0) to store the point with top y
    for(int dx = 0; dx < w; dx++){
        float topy = 0, top_score = -std::numeric_limits<float>::infinity();
        for(int dy = 1; dy < h - 1; dy ++){
            float score = scores(dx,dy-1) + scores(dx,dy) + scores(dx,dy+1);
            if ( type == Line::Type::BLACK || (type == Line::Type::ANY && score < 0))
                score = - score;

            if(score > top_score){
                top_score = score;
                topy = dy;
            }
        }

        scores(dx,0) = topy;
        avg_y_rel += topy;
    }

    avg_y_rel /= w;
    float avg_y = y0 + avg_y_rel;
    float avg_x = x0 + 0.5 * (w-1);

    float m = 0;
    for(int dx = 0; dx < w; dx++){
        m += (dx - 0.5 * (w-1)) * ( scores(dx,0) - avg_y_rel);
    }
    m /= w * ( w * w - 1) / 12.f;

    return {{avg_x,avg_y},m, type };
}