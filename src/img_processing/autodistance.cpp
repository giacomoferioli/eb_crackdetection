#include "img_processing/autodistance.hpp"


AutoDistance::AutoDistance(Config cfg) : FrameProcessor(cfg), output_file("data/autodistout.csv") {

}

void AutoDistance::on_setup(unsigned){
    
}

void AutoDistance::on_termination(unsigned){
    output_file.close();
}



float AutoDistance::line_score(int x, int y){
    return (PS(x,y) + PS(x,y+1) + PS(x, y-1) + PS(x,y+2) + PS(x, y-2)) / 5.f;
}

#define BORDER_SPACING 100
float AutoDistance::score(int y, float dist){
    float score = 0;
    int floordist = std::floor(dist);
    float w1 = (dist - floordist), w0 = 1 - w1;
    const int x1 = width - std::max(BORDER_SPACING, floordist + 1);
    for(int x = BORDER_SPACING ; x < x1; x++){
        score += PS(x,y) * ( w0 * PS(x+floordist,y) + w1 * PS(x+floordist+1,y) );
    }

    return score / (x1 - BORDER_SPACING);
}

#define Y0 134
#define CAMERA_H_FOV 0.722
#define CAMERA_H_RES 1280

#include <iostream>

ProcessResult AutoDistance::process_frame(unsigned int framenum){

    float l = 1 / (cfg.camera_distance * CAMERA_H_FOV / CAMERA_H_RES);
    
    float margin = 0.25;

    for(int i = 0; i < 4; i++){

        float topscore = score(Y0, l);
        float topguess = l;

        for(float guess = l * (1-margin); guess < l * (1+margin); guess += l / 50){
            const float v = score(Y0,guess);
            if( v > topscore){
                topguess = guess;
                topscore = v; 
            }
        }
        l = topguess * 2;
        margin /= 2;

    }

    l /= 16;
    


    if(framenum == 127){
        img_saver.reset(new ImageSaver(
            [this] (int ix) { 
                return  ( std::clamp(16 * PS(ix)+128,0,255) << 24) + 0x0000FF;
            },width,height,"proc_images"));
        img_saver->save_bitmap("127.bmp");
        img_saver.reset(nullptr);
    }

    output_file << framenum * cfg.processing_interval << "," << l << "\n";

    return ProcessResult::OK;
}
