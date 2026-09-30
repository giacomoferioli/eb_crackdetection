#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class ImageSaveHelper {
public:
    enum class Format { Png, Bitmap };
    using ColorFunction = std::function<std::uint32_t(std::int16_t)>;

    ImageSaveHelper();

    void set_color_function(ColorFunction function);

    bool save(const std::vector<std::int16_t>& pixels, int width, int height, Format format, const std::string& name = "");
    bool save_png(const std::vector<std::int16_t>& pixels, int width, int height, const std::string& name = "");
    bool save_bitmap(const std::vector<std::int16_t>& pixels, int width, int height, const std::string& name = "");

    const std::string& last_error() const { return last_error_; }

private:
    std::string output_path(Format format, const std::string& name);
    void convert_pixels(const std::vector<std::int16_t>& pixels, std::size_t count);
    bool write_png(const std::string& path, int width, int height);
    bool write_bitmap(const std::string& path, int width, int height);

    ColorFunction pixel_to_color_;
    std::vector<std::uint8_t> bgr_pixels_;
    std::vector<std::uint8_t> bitmap_data_;
    std::uint64_t first_save_ms_ = 0;
    bool timer_started_ = false;
    std::string last_error_;
};
