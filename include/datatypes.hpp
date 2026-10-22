#pragma once

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

//used to find the max of an array
template<typename value_type>
struct Entry{
    int ix;
    value_type val;

    bool operator>(const Entry<value_type>& rhs) const {return val > rhs.val;}
    bool operator<(const Entry<value_type>& rhs) const {return val < rhs.val;}
    bool operator==(const Entry<value_type>& rhs) const {return val == rhs.val;}
};

struct Point{
    float x,y;
};

#define MAXM 0.2;
struct LinePoint : public Point{
    bool operator==(const LinePoint& rhs) const{
        if(std::abs(rhs.x - x) < 1e-3 && std::abs(rhs.y - y) < 1e-3) return true;
        return std::abs( (rhs.y - y) / (rhs.x - x) ) < MAXM;
    }
};

//used by clustering algorithms
template <typename T>
struct Group : public std::vector<T>{
    using std::vector<T>::vector;

    virtual bool could_be_member(const T& v) {
        for( const T& m : *this)
            if(!( m == v ))
                return false;
        return true;
    }

    bool operator>(const Group& rhs) const { return this->size() > rhs.size();}
};


struct Line{
    Point p;
    float m;

    enum Type{
        WHITE,
        BLACK,
        ANY,
        INVALID
    } type;
    float strength;

    float eval(float x) { return p.y + (x-p.x) * m;}

    explicit operator bool() const noexcept {return type != INVALID;}

    friend bool operator>(const Line& lhs, const Line& rhs){return lhs.strength > rhs.strength;}
    friend bool operator<(const Line& lhs, const Line& rhs){return lhs.strength < rhs.strength;}
};

struct SpecimenPosition{
    float top, bottom, thickness;
};



template<typename T>
struct BestN{
    using const_iterator = typename std::vector<T>::const_reverse_iterator;
    
    BestN(size_t n) : values(n), n(0) {};

    std::size_t size() const noexcept { return n; }
    bool empty() const noexcept { return n == 0; }

    const T& operator[](std::size_t index) const noexcept { return values[values.size() - 1 - index];}
    const T& at(std::size_t index) const {
        if (index >= n)
            throw std::out_of_range("BestN index out of range");
        return (*this)[index];
    }

    const_iterator begin() const noexcept { return values.crbegin(); }
    const_iterator end() const noexcept { return values.crbegin() + static_cast<std::ptrdiff_t>(n); }

    bool remove(std::size_t index) {
        if(index >= n)
            return false;

        const std::size_t storage_index = values.size() - 1 - index;
        const std::size_t first_active = values.size() - n;
        for(std::size_t i = storage_index; i > first_active; i--)
            values[i] = values[i - 1];

        n--;
        return true;
    }

    bool insert(const T& v) {
        int ix = -1;
        while( ( static_cast<size_t>(ix+1) < values.size() && v > values[ix+1]) || static_cast<size_t>(ix + 1) < values.size() - n)
            ix++;

        if(n < values.size())
            n++;

        if( ix >= 0){
            for(size_t i = 0; i < static_cast<size_t>(ix); i ++)
                values[i] = values[i+1];

            values[ix] = v;
            return true;
        }
        return false;
    }
    
    private:
    std::vector<T> values;
    size_t n;
};
