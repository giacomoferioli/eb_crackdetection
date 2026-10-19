#pragma once

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>
#include "datatypes.hpp"

namespace FrameCfg {
    constexpr int line_search_semi_interval = 3;
    // [HC] = 3, constexpr int line_thickness = 3;
} 


class Frame {
public:
    Frame() = default;

    Frame(int width, int height, float initial_value = 0.0F) {
        reset(width, height, initial_value);
    }

    Frame(int width, int height, std::vector<float> pixels)
        : width_(width), height_(height), pixels_(std::move(pixels)) {
        validate_dimensions(width_, height_);
        if (pixels_.size() != pixel_count()) {
            throw std::invalid_argument("Frame pixel count does not match its dimensions");
        }
    }

    void reset(int width, int height, float initial_value = 0.0F) {
        validate_dimensions(width, height);
        width_ = width;
        height_ = height;
        pixels_.assign(pixel_count(), initial_value);
    }

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] std::size_t size() const noexcept { return pixels_.size(); }
    [[nodiscard]] bool empty() const noexcept { return pixels_.empty(); }

    float& operator()(int x, int y) noexcept {
        return pixels_[index(x, y)];
    }

    const float& operator()(int x, int y) const noexcept {
        return pixels_[index(x, y)];
    }

    float& operator[](std::size_t index) noexcept { return pixels_[index]; }
    const float& operator[](std::size_t index) const noexcept { return pixels_[index]; }

    float& at(int x, int y) {
        check_coordinates(x, y);
        return pixels_.at(index(x, y));
    }

    const float& at(int x, int y) const {
        check_coordinates(x, y);
        return pixels_.at(index(x, y));
    }

    void fill(float value) {
        std::fill(pixels_.begin(), pixels_.end(), value);
    }

    float* data() noexcept { return pixels_.data(); }
    const float* data() const noexcept { return pixels_.data(); }

    auto begin() noexcept { return pixels_.begin(); }
    auto end() noexcept { return pixels_.end(); }
    auto begin() const noexcept { return pixels_.begin(); }
    auto end() const noexcept { return pixels_.end(); }

    // Math function

    Line find_hline(int x0,int y0, int w, int h,Line::Type type = Line::Type::ANY) const;

    //

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
            throw std::invalid_argument("Frame dimensions cannot be negative");
        }
    }

    void check_coordinates(int x, int y) const {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) {
            throw std::out_of_range("Frame coordinates are out of bounds");
        }
    }

    int width_ = 0;
    int height_ = 0;
    std::vector<float> pixels_;
};
