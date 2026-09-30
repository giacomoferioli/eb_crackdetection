#include "image_save_helper.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

namespace {
std::uint64_t current_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void write_u16(std::vector<std::uint8_t>& data, std::size_t offset, std::uint16_t value) {
    data[offset] = value & 0xFF;
    data[offset + 1] = value >> 8;
}

void write_u32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value) {
    data[offset] = value & 0xFF;
    data[offset + 1] = value >> 8;
    data[offset + 2] = value >> 16;
    data[offset + 3] = value >> 24;
}
}

ImageSaveHelper::ImageSaveHelper() {
    set_color_function([](std::int16_t pixel) {
        std::uint32_t gray = std::clamp<int>(pixel, 0, 255);
        return gray << 24 | gray << 16 | gray << 8 | 0xFF;
    });
}

void ImageSaveHelper::set_color_function(ColorFunction function) {
    pixel_to_color_ = std::move(function);
}

bool ImageSaveHelper::save(const std::vector<std::int16_t>& pixels, int width, int height, Format format, const std::string& name) {
    last_error_.clear();
    if (pixels.size() < std::size_t(width) * height) {
        last_error_ = "The pixel vector is smaller than the image dimensions";
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories("images", error);
    if (error) {
        last_error_ = "Could not create the images directory";
        return false;
    }

    convert_pixels(pixels, std::size_t(width) * height);
    std::string path = output_path(format, name);
    return format == Format::Png ? write_png(path, width, height) : write_bitmap(path, width, height);
}

bool ImageSaveHelper::save_png(const std::vector<std::int16_t>& pixels, int width, int height, const std::string& name) {
    return save(pixels, width, height, Format::Png, name);
}

bool ImageSaveHelper::save_bitmap(const std::vector<std::int16_t>& pixels, int width, int height, const std::string& name) {
    return save(pixels, width, height, Format::Bitmap, name);
}

std::string ImageSaveHelper::output_path(Format format, const std::string& name) {
    std::string filename = name;
    if (filename.empty()) {
        std::uint64_t now = current_ms();
        if (!timer_started_) {
            first_save_ms_ = now;
            timer_started_ = true;
        }
        filename = std::to_string(now - first_save_ms_);
    }

    std::filesystem::path path = std::filesystem::path(filename).filename();
    path.replace_extension(format == Format::Png ? ".png" : ".bmp");
    return (std::filesystem::path("images") / path).string();
}

void ImageSaveHelper::convert_pixels(const std::vector<std::int16_t>& pixels, std::size_t count) {
    bgr_pixels_.resize(count * 3);
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t color = pixel_to_color_(pixels[i]);
        bgr_pixels_[i * 3] = color >> 8;
        bgr_pixels_[i * 3 + 1] = color >> 16;
        bgr_pixels_[i * 3 + 2] = color >> 24;
    }
}

bool ImageSaveHelper::write_png(const std::string& path, int width, int height) {
    try {
        cv::Mat image(height, width, CV_8UC3, bgr_pixels_.data());
        if (cv::imwrite(path, image, {cv::IMWRITE_PNG_COMPRESSION, 1})) return true;
        last_error_ = "OpenCV could not encode the PNG";
    } catch (const cv::Exception& exception) {
        last_error_ = exception.what();
    }
    return false;
}

bool ImageSaveHelper::write_bitmap(const std::string& path, int width, int height) {
    std::size_t row_size = (std::size_t(width) * 3 + 3) & ~std::size_t(3);
    std::size_t pixel_data_size = row_size * height;
    if (pixel_data_size > std::numeric_limits<std::uint32_t>::max() - 54) {
        last_error_ = "The bitmap is too large";
        return false;
    }

    bitmap_data_.assign(54 + pixel_data_size, 0);
    bitmap_data_[0] = 'B';
    bitmap_data_[1] = 'M';
    write_u32(bitmap_data_, 2, bitmap_data_.size());
    write_u32(bitmap_data_, 10, 54);
    write_u32(bitmap_data_, 14, 40);
    write_u32(bitmap_data_, 18, width);
    write_u32(bitmap_data_, 22, -height);
    write_u16(bitmap_data_, 26, 1);
    write_u16(bitmap_data_, 28, 24);
    write_u32(bitmap_data_, 34, pixel_data_size);

    for (int y = 0; y < height; ++y)
        std::copy_n(bgr_pixels_.data() + std::size_t(y) * width * 3, std::size_t(width) * 3, bitmap_data_.data() + 54 + std::size_t(y) * row_size);

    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bitmap_data_.data()), bitmap_data_.size());
    if (output) return true;
    last_error_ = "Could not write the bitmap";
    return false;
}
