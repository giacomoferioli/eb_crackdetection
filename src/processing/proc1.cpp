#include "processing/proc1.hpp"
#include "image_saver.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream> 
#include <vector>

Proc1::Proc1(EventSource& source, Config cfg) : Processing(source,cfg) {
    
}

#define OVERLAYSCALING 32
#define RAWSCALING 32

void Proc1::on_setup() {
    img_saver.reset(new ImageSaver(
    [this] (int ix) { 
        int gray = std::clamp(128 + PS(ix) * RAWSCALING,0,255);
        int red = std::clamp( OVERLAYSCALING * frame[ix],0,255 );
        int blue = std::clamp( OVERLAYSCALING * -frame[ix],0,255 );
        
        return 0xFF 
        | (red + gray * (256-red) / 256) << 24
        | (gray) << 16
        | (blue + gray * (256-blue) / 256) << 8;
    },
    width,height,"proc_images/"
    ));

    matrix = new int[9];
    matrix[0] = matrix[1] = matrix[2] = -1;
    matrix[3] = matrix[4] = matrix[5] = 1;
    matrix[6] = matrix[7] = matrix[8] = -1;
}

//do not call on frontier points
inline int Proc1::score(int x0, int y0, int l) const{
    int score = 0;
    for(int x = x0; x <= x0+l; x++){
        score += PS(x,y0+1) + PS(x,y0); // -PS(x,y0) - PS(x,y0-1);
    }
    return score;
}

void Proc1::process_frame(){
    static int framenum = 0;
    frame.assign(height * width, 0);

    std::vector<int> topy;
    std::vector<int> topv;

    int x0 = width / 6;
    
    for(int y = 1; y < 3 * height / 4; y++){
        frame[y * width + x0] = score(x0,y,15);

        int s = abs(frame[y * width + x0]);
        bool skip = false;
        for(int ty : topy)
            skip = skip | (abs(ty - y) < 6);
        
        if(skip)
            continue;
        
        if(topy.size() == 0){
            topy.push_back(y);
            topv.push_back(s);
        }else if(s > topv[0] || topy.size() < 4) {
            size_t ix = 0;
            while(ix < topy.size() && s > topv[ix]) 
                ix++;

            if( ix == topy.size()){
                topy.push_back(y);
                topv.push_back(s);
            }
            else{
                topy.insert(topy.begin() + ix,y);
                topv.insert(topv.begin() + ix,s);
            }

            if(topy.size() > 4){
                topy.erase(topy.begin());
                topv.erase(topv.begin());
            }
        }
    }

    for(int y0 : topy){
        int yc = y0;
        
        for(int x = x0; x < width - 10; x++){
            for(int y = yc-5; y <= yc+5; y++){
                if (y < 2 || y >= height - 2)
                    continue;
                frame[y * width + x] = score(x,y,7);
            }

            if( abs(frame[(yc + 1) * width + x]) > abs(frame[yc * width + x])) yc++;
            if( abs(frame[(yc - 1) * width + x]) > abs(frame[yc * width + x])) yc--;
        }
    }

    if((++framenum & 0xFF) == 0) std::cout << "\r" << (double)framenum * cfg.processing_interval / 1e6 << "      " << std::flush;
}