#pragma once


struct Point{
    float x,y;
};

struct Line{
    Point p;
    float m;

    enum Type{
        WHITE,
        BLACK,
        ANY
    } type;

    float eval(float x) { return p.y + (x-p.x) * m;}
};