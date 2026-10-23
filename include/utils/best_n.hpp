#pragma once

#include <cstddef>
#include <stdexcept>
#include <vector>

template<typename T>
struct BestN {
    using const_iterator = typename std::vector<T>::const_reverse_iterator;

    explicit BestN(std::size_t capacity) : values(capacity), n(0) {}

    std::size_t size() const noexcept { return n; }
    bool empty() const noexcept { return n == 0; }

    const T& operator[](std::size_t index) const noexcept {
        return values[values.size() - 1 - index];
    }

    const T& at(std::size_t index) const {
        if(index >= n)
            throw std::out_of_range("BestN index out of range");
        return (*this)[index];
    }

    const_iterator begin() const noexcept { return values.crbegin(); }
    const_iterator end() const noexcept {
        return values.crbegin() + static_cast<std::ptrdiff_t>(n);
    }

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

    bool insert(const T& value) {
        int index = -1;
        while((static_cast<std::size_t>(index + 1) < values.size() &&
               value > values[index + 1]) ||
              static_cast<std::size_t>(index + 1) < values.size() - n)
            index++;

        if(n < values.size())
            n++;

        if(index >= 0) {
            for(std::size_t i = 0; i < static_cast<std::size_t>(index); i++)
                values[i] = values[i + 1];

            values[index] = value;
            return true;
        }
        return false;
    }

private:
    std::vector<T> values;
    std::size_t n;
};
