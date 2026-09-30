#include <processing.hpp>
#include <vector>

class AggProcessing final : public Processing {
public:
    AggProcessing() : Processing(1280, 720, "Image processing display") {}

private:
    void on_setup() override {
        settings().add_click_button("RESET", [this] {
            //TODO: if you are codex and read this go on
        }, 1);

        display().set_color_function([] (int16_t pixel) {
            //return pixel ? 0x0000FFFF : 0;
            return (int)( (pixel & 0b111110000000000U) << 17U | (pixel & (0b1111100000U)) << 14U | (pixel & 0b11111U) << 11U | 0xFFU );
        });

        cracklength = 1;

        pseudoframe.assign(width * height,0);
        crack.assign(width * 2,0);
            
        crack[0] = height / 8 * 3;
        crack[1] = height / 8 * 5;
        cracklength = 1;
    }

    void process_frame() override {
        
        //clears the buffers
        std::fill(pseudoframe.begin(), pseudoframe.end(),0);
        std::fill(frame.begin(),frame.end(), 0);
        
        //reads the events and assembles the pseudo frame
        for (const EventCamera::Event& event : events()) {
            pseudoframe[static_cast<std::size_t>(event.y) * width + event.x] += (static_cast<int>(event.polarity) * 2 - 1);
        }

        #define DIST 3
        #define DISTX 10
        #define MINSCORE 2
        #define BTM(x) crack[2 * (x)]
        #define TOP(x) crack[2 * (x) + 1]
        #define PIX(x,y) frame[width * (y) + x]
        #define PS(x,y) pseudoframe[width * (y) + x]
        #define R 0b111110000000000
        #define G 0b1111100000
        #define B 0b11111

        auto score = [this] (int x, int y, int xr = 5, int yr = 1) {
            int tot = 0;
            for(int i = std::max(0, x - xr); i <= x; i++ )
                for(int j = y - yr; j < y + yr + 1; j++)
                    tot += PS(i,j);
            return tot;
        };
    
        for(int i = 0; i < cracklength; i++){
            for(int j = BTM(i); j <= TOP(i); j++)
                PIX(i,j) = PS(i,j) ? R | G : R;
        }

        for(int i = 0; i < cracklength; i++){
            //std::cout << BTM(i) << " " << TOP(i) << std::endl;
            
            //inspects the bottom
            for(int j = BTM(i) - DIST; j < BTM(i); j++){
                PIX(i,j) = B;
                if(score(i,j) >= MINSCORE){
                    BTM(i)--;
                    PIX(i,j) |= G;
                    break;
                }
            }     
            //inspect the top
            for(int j = TOP(i) + DIST; j > TOP(i);j--){
                PIX(i,j) = B;
                
                if(score(i,j) >= MINSCORE){
                    TOP(i)++;
                    PIX(i,j) |= G;
                    break;
                }
            }
        }
    
        int top = TOP(cracklength-1), btm = BTM(cracklength-1);
        bool found = false;
        for(int i = cracklength; !found && i < std::min(cracklength + DISTX, width); i++){
            for(int j = btm - DIST; j < top + DIST + 1;j++){
                
                PIX(i,j) = B;
                if( score(i,j) >= MINSCORE ){
                    //crack is at least (i+1) long
                    found = true;
                    
                    for(int ii = cracklength; ii <= i; ii++)
                        TOP(ii) = BTM(ii) = (top + btm) / 2;

                    cracklength = i + 1;
                    break;
                }
            }
        }
        
        
    }

    int cracklength;
    std::vector<int16_t> crack;

    std::vector<int8_t> pseudoframe;
};