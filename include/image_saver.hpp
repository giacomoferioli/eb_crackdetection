#pragma once

#include "timing.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>


#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>


inline void write_u16(std::vector<std::uint8_t>& data, std::size_t offset, std::uint16_t value) {
    data[offset] = value & 0xFF;
    data[offset + 1] = value >> 8;
}

inline void write_u32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value) {
    data[offset] = value & 0xFF;
    data[offset + 1] = value >> 8;
    data[offset + 2] = value >> 16;
    data[offset + 3] = value >> 24;
}

template <typename T>
class ImageSaver {
public:
    enum class Format { Png, Bitmap };
    
    ImageSaver(std::vector<T>& img,int width,int height,std::filesystem::path dst_dir,std::function<unsigned int (T)> getcolor) : width(width), height(height), img(img), getcolor(getcolor), dst_dir_(dst_dir) {};

    bool save(Format format, const std::string& name = ""){
        last_error_.clear();
        std::error_code error;
        std::filesystem::create_directories("images", error);
        if (error) {
            last_error_ = "Could not create the images directory";
            return false;
        }

        bgr_pixels_.resize((width*height) * 3);
        for (int i = 0; i < width * height; ++i) {
            std::uint32_t color = getcolor(img[i]);
            bgr_pixels_[i * 3] = color >> 8;
            bgr_pixels_[i * 3 + 1] = color >> 16;
            bgr_pixels_[i * 3 + 2] = color >> 24;
        }
        std::string path = output_path(format, name);
        return format == Format::Png ? write_png(path, width, height) : write_bitmap(path, width, height);
    }
    bool save_png(const std::string& name = "") {return save(Format::Png, name);}
    bool save_bitmap(const std::string& name = "") {return save(Format::Bitmap, name);}

    const std::string& last_error() const { return last_error_; }

private:
    int width, height;
    std::vector<T>& img;
    std::function<int (T)> getcolor;

    std::string output_path(Format format, const std::string& name){
        std::string filename = name;
        if (filename.empty()) {
            std::uint64_t now = timing::ms();
            if (!timer_started_) {
                first_save_ms_ = now;
                timer_started_ = true;
            }
            filename = std::to_string(now - first_save_ms_);
        }

        std::filesystem::path path = std::filesystem::path(filename).filename();
        path.replace_extension(format == Format::Png ? ".png" : ".bmp");
        return (dst_dir_ / path).string();
    }
    void convert_pixels();
    bool write_png(const std::string& path, int width, int height){
        try {
            cv::Mat image(height, width, CV_8UC3, bgr_pixels_.data());
            if (cv::imwrite(path, image, {cv::IMWRITE_PNG_COMPRESSION, 1})) return true;
            last_error_ = "OpenCV could not encode the PNG";
        } catch (const cv::Exception& exception) {
            last_error_ = exception.what();
        }
        return false;
    }
    bool write_bitmap(const std::string& path, int width, int height){
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

    std::vector<std::uint8_t> bgr_pixels_;
    std::vector<std::uint8_t> bitmap_data_;
    std::uint64_t first_save_ms_ = 0;
    bool timer_started_ = false;
    std::string last_error_;
    std::filesystem::path dst_dir_;
};
