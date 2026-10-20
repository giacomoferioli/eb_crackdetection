#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

// Maximum memory occupied by the element array of one circular buffer.
#define CIRCULAR_BUFFER_MAX_MEMORY_BYTES (1ULL << 30)

template <typename T>
class CircularBuffer {
    public:
    CircularBuffer(unsigned length) : start(0), stop(0), cnt(0), length(length),original_length(length){
        data = new T[length];
    }

    ~CircularBuffer(){
        delete[] data;
    }

    void append(T v) {
        if(cnt == length) grow();
        data[stop] = v;
        stop = (stop+1) % length;
        cnt++;
    }
    T peek() const { return data[start]; }
    T pop() {
        T value = data[start];
        start = (start+1) % length;
        cnt--;
        if(length > original_length && cnt < length/4)
            resize(length/2);
        return value;
    }

    bool empty() const { return cnt == 0; }
    int count() const { return cnt; }
    void clear() { start = stop = cnt = 0; }

    private:
        void grow(){
            unsigned long long new_length = 2ULL * length;
            if(new_length > CIRCULAR_BUFFER_MAX_MEMORY_BYTES / sizeof(T))
                throw std::runtime_error("circular buffer cannot grow beyond 1 GiB");
            resize(new_length);
        }

        // Copy from logical FIFO order so start becomes zero after resizing.
        void resize(unsigned new_length){
            T* new_data = new T[new_length];
            for(unsigned i = 0; i < cnt; i++)
                new_data[i] = data[(start+i) % length];
            delete[] data;
            data = new_data;
            start = 0;
            stop = cnt;
            length = new_length;
        }

        T* data;
        unsigned int start,stop,cnt;
        unsigned length;
        const unsigned original_length;

};

template <typename scalar>
scalar kill_outliers(const std::vector<scalar>& data, size_t ix, int num){
    int i0 = (int)ix - num/2;
    size_t i1 = ix + num/2 + (num & 1);
    while(i0 < 0){
        i0++;
        i1++;
    }
    
    while(i1 > data.size()){
        i0--;
        i1--;
    }
    if(i0 < 0)
        i0 = 0;

    num = i1 - i0;

    float avg=0, R2=0;
    for(size_t i = i0; i < i1;i++){
        if(i == ix)
            continue;
        avg += data[i];
    }
    avg /= (num-1);

    for(size_t i = i0; i < i1; i++){
        if(i == ix)
            continue;

        R2 += pow(data[i] - avg,2);
    }
    R2 /= (num-1);

    if( pow(data[ix]-avg,2) > 3 * R2 )
        return avg;
    else
        return data[ix];

}
