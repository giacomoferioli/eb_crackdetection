#include "processing/proc1.hpp"
#include "image_saver.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream> 
#include <vector>

Proc1::Proc1(EventSource& source, Config cfg) : Processing(source,cfg) {
    
}

#define OVERLAYSCALING 16
#define RAWSCALING 32

void Proc1::on_setup() {
    img_saver.reset(new ImageSaver(
    [this] (int ix) { 
        int gray = std::clamp(128 + PS(ix) * RAWSCALING,0,255);
        if( (frame[ix] & 0xFF) == 0)
            return gray << 24 | gray << 16 | gray << 8 | 0xFF;
        return 0xFF 
        | (std::clamp( OVERLAYSCALING * (frame[ix] & 0xFF),0,255 ) + gray ) / 2 << 24
        | ( gray / 2) << 16
        | (std::clamp( OVERLAYSCALING * (-frame[ix] & 0xFF),0,255 ) + gray ) / 2 << 8;
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
        score += PS(x,y0+2) + PS(x,y0+1) -PS(x,y0) - PS(x,y0-1);
    }
    return score;
}

void Proc1::process_frame(){
    static int framenum = 0;
    frame.assign(height * width, 0);

    std::vector<int> topy;
    std::vector<int> topv;

    int x0 = width / 6;
    
    for(int y = 4; y < height - 4; y++){
        frame[y * width + x0] = score(x0,y,5);

        int s = abs(frame[y * width + x0]);
        if(topy.size() == 0){
            topy.push_back(y);
            topv.push_back(s);
        }else if(s > topv[0]) {
            size_t ix = 0;
            while(ix < topy.size() && s > topv[ix]) 
                ix++;

            if( ix == topy.size()){
                topy.push_back(y);
                topv.push_back(s);
            }
            else{
                topy.insert(topy.begin() + ix - 1,y);
                topv.insert(topv.begin() + ix - 1,s);
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
                frame[y * width + x] = score(x,y,4);
            }

            if( abs(frame[(yc + 1) * width + x]) > abs(frame[yc * width + x])) yc++;
            if( abs(frame[(yc - 1) * width + x]) > abs(frame[yc * width + x])) yc--;
        }
    }

    if(framenum++ & 256) std::cout << "." << std::flush;
}