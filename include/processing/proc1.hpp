#pragma once
#include "processing/processing.hpp"

class Proc1 : public Processing{
    public:
    Proc1(EventSource& source, Config cfg);
    
    protected:

    void on_setup() override;
    void process_frame() override;

    private:
    int* matrix;
    std::vector<std::int16_t> frame;
};