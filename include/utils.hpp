#pragma once

#include "datatypes.hpp"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include <functional>
#include <optional>

// Maximum memory occupied by the element array of one circular buffer.
#define CIRCULAR_BUFFER_MAX_MEMORY_BYTES (1ULL << 30)

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

    ~CircularBuffer(){
        delete[] data;
    }

    void append(T v) {
        if(cnt == length && !grow()){
            warn_about_overwrite();
            data[stop] = v;
            stop = (stop + 1) % length;
            start = stop;
            return;
        }

        data[stop] = v;
        stop = (stop + 1) % length;
        cnt++;
    }
    T peek() const { return data[start]; }
    T pop() {
        T value = data[start];
        start = (start+1) % length;
        cnt--;
        if(length > original_length && cnt < length/4)
            resize(length / 2 < original_length ? original_length : length / 2);
        return value;
    }

    bool empty() const { return cnt == 0; }
    int count() const { return cnt; }
    void clear() { start = stop = cnt = 0; }

    private:
        static constexpr std::size_t maximum_length() {
            return CIRCULAR_BUFFER_MAX_MEMORY_BYTES / sizeof(T);
        }

        bool grow(){
            if(overflow_behavior == CircularBufferOverflowBehavior::Overwrite)
                return false;

            const std::size_t maximum = maximum_length();
            if(length > maximum / 2){
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

        void warn_about_overwrite(){
            if(overwrite_warning_emitted)
                return;
            std::cerr << "warning: circular buffer reached its capacity; "
                         "new values will overwrite the oldest values\n";
            overwrite_warning_emitted = true;
        }

        // Copy from logical FIFO order so start becomes zero after resizing.
        void resize(std::size_t new_length){
            T* new_data = new T[new_length];
            for(std::size_t i = 0; i < cnt; i++)
                new_data[i] = data[(start+i) % length];
            delete[] data;
            data = new_data;
            start = 0;
            stop = cnt;
            length = new_length;
        }

        T* data;
        std::size_t start,stop,cnt;
        std::size_t length;
        const std::size_t original_length;
        CircularBufferOverflowBehavior overflow_behavior;
        bool overwrite_warning_emitted = false;

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

//given a vector of guesses (possible values with == defined (in this context == means close enough not equal)) it selects the best one
template <typename Guess>
std::optional<Guess> select_best_guess(const std::vector<Guess>& candidates, unsigned min_agreeing, unsigned max_disagreeing, std::function<Guess(const std::vector<Guess>&)> average_fun = {}){
    if( candidates.size() < min_agreeing || candidates.size() == 0)
        return std::nullopt;

    Guess best_guess = candidates[0];
    unsigned best_guess_n = 1;
    for(size_t i = 0; i < candidates.size(); i++){
        Guess guess = candidates[i];
        unsigned guess_n = 1;

        //count number of matching guesses
        for(size_t j = 0; j < candidates.size(); j++){
            if(i == j) continue;

            if(candidates[j] == guess)
                guess_n++;
        }

        if(guess_n > best_guess_n){
            //calculates the average
            best_guess_n = guess_n;

            if (average_fun){
                std::vector<Guess> selected;
                for(size_t j = 0; j < candidates.size(); j++)
                    if(candidates[j] == guess)
                        selected.push_back(candidates[j]);
                best_guess = average_fun(selected);
            }
            else
                best_guess = guess;
        }
    }

    if( best_guess_n < min_agreeing || (candidates.size() - best_guess_n > max_disagreeing))
        return std::nullopt;

    return best_guess;
}

//given a vector of objects it uses thei
template <typename T>
BestN<Group<T>> select_biggest_groups(const std::vector<T>& candidates, unsigned min_size,size_t num){
    BestN<Group<T>> best(num);

    if( candidates.size() < min_size || candidates.size() == 0)
        return best;

    std::vector<bool> selected(candidates.size());
    for(size_t i = 0; i < candidates.size(); i++){
        if(selected[i])
            continue;

        Group<T> guess = {candidates[i]};
        std::vector<size_t> selected_indexes = {i};
        //count number of matching guesses
        for(size_t j = 0; j < candidates.size(); j++){
            if( i == j || selected[j])
                continue;
            if(guess.could_be_member(candidates[j])){
                guess.push_back(candidates[j]);
                selected_indexes.push_back(j);
            }
        }

        if( guess.size() >= min_size){    
            best.insert(guess);
            
            for( size_t ix : selected_indexes)
                selected[ix] = true;
        }
    }
    
    return best;
}

template <typename T, typename CustomGroup>
BestN<CustomGroup> select_biggest_groups(const std::vector<T>& candidates, unsigned min_size,size_t num){
    BestN<CustomGroup> best(num);
    static_assert(
      std::is_base_of<Group<T>,CustomGroup>(),
      "CustomGroup must be default-constructible"
    );

    if( candidates.size() < min_size || candidates.size() == 0)
        return best;

    std::vector<bool> selected(candidates.size());
    for(size_t i = 0; i < candidates.size(); i++){
        if(selected[i])
            continue;

        CustomGroup guess = { {candidates[i]}, 1};
        std::vector<size_t> selected_indexes = {i};
        //count number of matching guesses
        for(size_t j = 0; j < candidates.size(); j++){
            if( i == j)
                continue;
            if(guess.could_be_member(candidates[j])){
                guess.num++;
                guess.values.push_back(candidates[j]);
                selected_indexes.push_back(j);
            }
        }

        if( guess.num >= min_size){    
            best.insert(guess);
            
            for( size_t ix : selected_indexes)
                selected[ix] = true;
        }
    }
    
    return best;
}
