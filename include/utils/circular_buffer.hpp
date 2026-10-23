#pragma once

#include <cstddef>
#include <iostream>
#include <stdexcept>

enum class CircularBufferOverflowBehavior {
    GrowOrThrow,
    GrowThenOverwrite,
    Overwrite
};

template <typename T>
class CircularBuffer {
public:
    explicit CircularBuffer(
        std::size_t length,
        CircularBufferOverflowBehavior overflow_behavior =
            CircularBufferOverflowBehavior::GrowThenOverwrite)
        : start(0), stop(0), cnt(0), length(length),
          original_length(length), overflow_behavior(overflow_behavior) {
        if(length == 0)
            throw std::invalid_argument("circular buffer length must be greater than zero");
        if(length > maximum_length())
            throw std::runtime_error("circular buffer cannot exceed 1 GiB");
        data = new T[length];
    }

    ~CircularBuffer() {
        delete[] data;
    }

    void append(T value) {
        if(cnt == length && !grow()) {
            warn_about_overwrite();
            data[stop] = value;
            stop = (stop + 1) % length;
            start = stop;
            return;
        }

        data[stop] = value;
        stop = (stop + 1) % length;
        cnt++;
    }

    T peek() const { return data[start]; }

    T pop() {
        T value = data[start];
        start = (start + 1) % length;
        cnt--;
        if(length > original_length && cnt < length / 4)
            resize(length / 2 < original_length ? original_length : length / 2);
        return value;
    }

    bool empty() const { return cnt == 0; }
    int count() const { return cnt; }
    void clear() { start = stop = cnt = 0; }

private:
    static constexpr std::size_t maximum_memory_bytes = 1ULL << 30;

    static constexpr std::size_t maximum_length() {
        return maximum_memory_bytes / sizeof(T);
    }

    bool grow() {
        if(overflow_behavior == CircularBufferOverflowBehavior::Overwrite)
            return false;

        const std::size_t maximum = maximum_length();
        if(length > maximum / 2) {
            if(overflow_behavior == CircularBufferOverflowBehavior::GrowOrThrow)
                throw std::runtime_error("circular buffer cannot grow beyond 1 GiB");
            if(length == maximum)
                return false;
            resize(maximum);
            return true;
        }

        resize(length * 2);
        return true;
    }

    void warn_about_overwrite() {
        if(overwrite_warning_emitted)
            return;
        std::cerr << "warning: circular buffer reached its capacity; "
                     "new values will overwrite the oldest values\n";
        overwrite_warning_emitted = true;
    }

    // Copy from logical FIFO order so start becomes zero after resizing.
    void resize(std::size_t new_length) {
        T* new_data = new T[new_length];
        for(std::size_t i = 0; i < cnt; i++)
            new_data[i] = data[(start + i) % length];
        delete[] data;
        data = new_data;
        start = 0;
        stop = cnt;
        length = new_length;
    }

    T* data;
    std::size_t start, stop, cnt;
    std::size_t length;
    const std::size_t original_length;
    CircularBufferOverflowBehavior overflow_behavior;
    bool overwrite_warning_emitted = false;
};
