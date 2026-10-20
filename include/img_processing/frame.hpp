#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#include "datatypes.hpp"

namespace FrameCfg {
    constexpr int line_search_semi_interval = 3;
    // [HC] = 3, constexpr int line_thickness = 3;
} 


template<typename T>
class BasicFrame {
    static_assert(
        std::is_arithmetic_v<T> &&
        !std::is_same_v<T,bool>,
        "BasicFrame requires a non-boolean arithmetic type"
    );


public:
    BasicFrame() = default;

    BasicFrame(int width, int height, T initial_value = T{}) {
        reset(width, height, initial_value);
    }

    BasicFrame(int width, int height, std::vector<T> pixels)
        : width_(width), height_(height), pixels_(std::move(pixels)) {
        validate_dimensions(width_, height_);
        if (pixels_.size() != pixel_count()) {
            throw std::invalid_argument("BasicFrame pixel count does not match its dimensions");
        }
    }

    void reset(int width, int height, T initial_value = T{}) {
        validate_dimensions(width, height);
        width_ = width;
        height_ = height;
        pixels_.assign(pixel_count(), initial_value);
    }

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] std::size_t size() const noexcept { return pixels_.size(); }
    [[nodiscard]] bool empty() const noexcept { return pixels_.empty(); }

    T& operator()(int x, int y) noexcept {
        return pixels_[index(x, y)];
    }

    const T& operator()(int x, int y) const noexcept {
        return pixels_[index(x, y)];
    }

    T& operator[](std::size_t index) noexcept { return pixels_[index]; }
    const T& operator[](std::size_t index) const noexcept { return pixels_[index]; }

    T& at(int x, int y) {
        check_coordinates(x, y);
        return pixels_.at(index(x, y));
    }

    const T& at(int x, int y) const {
        check_coordinates(x, y);
        return pixels_.at(index(x, y));
    }

    void fill(T value) {
        std::fill(pixels_.begin(), pixels_.end(), value);
    }

    T* data() noexcept { return pixels_.data(); }
    const T* data() const noexcept { return pixels_.data(); }

    auto begin() noexcept { return pixels_.begin(); }
    auto end() noexcept { return pixels_.end(); }
    auto begin() const noexcept { return pixels_.begin(); }
    auto end() const noexcept { return pixels_.end(); }

private:
    [[nodiscard]] std::size_t pixel_count() const noexcept {
        return static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    }

    [[nodiscard]] std::size_t index(int x, int y) const noexcept {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width_)
             + static_cast<std::size_t>(x);
    }

    static void validate_dimensions(int width, int height) {
        if (width < 0 || height < 0) {
            throw std::invalid_argument("BasicFrame dimensions cannot be negative");
        }
    }

    void check_coordinates(int x, int y) const {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) {
            throw std::out_of_range("BasicFrame coordinates are out of bounds");
        }
    }

    int width_ = 0;
    int height_ = 0;
    std::vector<T> pixels_;
};

class Frame : public BasicFrame<float> {
public:
    using BasicFrame<float>::BasicFrame;

    Line find_hline(int x0,int y0, int w, int h,Line::Type type = Line::Type::ANY) const;
};
