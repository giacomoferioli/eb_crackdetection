#include <stdexcept>

template <typename T>
class CircularBuffer {
    public:
    CircularBuffer(unsigned length) : start(0), stop(0), cnt(0), length(length){
        data = new T[length];
    }

    ~CircularBuffer(){
        delete[] data;
    }

    void append(T v) { data[stop] = v; stop = (stop+1) % length; cnt++; if(cnt == length) throw std::runtime_error("buffer full");}
    T peek() const { return data[start]; }
    T pop() { cnt--; int ix = start; start = (start+1) % length; return data[ix]; }

    bool empty() const { return cnt == 0; }
    int count() const { return cnt; }

    private:
        T* data;
        unsigned int start,stop,cnt;
        const unsigned length;

};
