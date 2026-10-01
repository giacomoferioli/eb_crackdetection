#include "processing/proc1.hpp"
#include "image_saver.hpp"

#include <algorithm>
#include <iostream> 

Proc1::Proc1(EventSource& source, Config cfg) : Processing(source,cfg) {
    
}

#define ALPHA 50

void Proc1::on_setup() {
    img_saver.reset(new ImageSaver<std::int16_t>(frame,width,height,"proc_images/",
    [] (std::int16_t value) {
        return std::clamp(4 * value,0,255) << 24U | std::clamp(4 * -value, 0, 255) << 8U | 0xFFU;
    }
    ));

    matrix = new int[9];
    matrix[0] = matrix[1] = matrix[2] = -1;
    matrix[3] = matrix[4] = matrix[5] = 1;
    matrix[6] = matrix[7] = matrix[8] = -1;
}

void Proc1::process_frame(){
    frame.assign(height * width, 0);

    for(int x = 1 ; x < width - 1; x ++){
        for(int y = 1; y < height - 1; y++){
            frame[y * width + x] = -1 * ( PS(x-1,y-1) + PS(x,y-1) + PS(x+1,y-1) + PS(x-1,y+1) + PS(x,y+1) + PS(x+1,y+1) ) + PS(x-1,y) + PS(x,y) + PS(x+1,y);
        }
    }

    std::cout << "." << std::flush;
}