#include "display_window.hpp"

#include "timing.hpp"

#include <algorithm>
#include <cmath>

DisplayWindow::DisplayWindow(){
    this->set_color_function(
        [] (std::int16_t pixel) {
            const auto grey = static_cast<std::uint32_t>(std::clamp<int>(pixel * 127 + 64, 0, 255));
            return (grey << 24U) | (grey << 16U) | (grey << 8U) | 0xFFU;
        }
    );
}

DisplayWindow::~DisplayWindow() { close(); }

bool DisplayWindow::create(const std::string& title, int width, int height) {
    close();
    if (width <= 0 || height <= 0) {
        SDL_SetError("Display resolution must be positive");
        return false;
    }

    window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, width, height,
                               SDL_WINDOW_SHOWN);
    if (!window_) {
        return false;
    }
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_) {
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer_) {
        close();
        return false;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    open_ = true;
    if (!set_resolution(width, height)) {
        close();
        return false;
    }
    return true;
}

void DisplayWindow::close() {
    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    color_buffer_.clear();
    open_ = false;
    last_frame_draw_us_ = 0;
}

void DisplayWindow::show() {
    if (window_) SDL_ShowWindow(window_);
}

void DisplayWindow::hide() {
    if (window_) SDL_HideWindow(window_);
}

bool DisplayWindow::set_resolution(int width, int height) {
    if (!renderer_ || width <= 0 || height <= 0) {
        return false;
    }
    const auto pixel_count = static_cast<std::size_t>(width) *
                             static_cast<std::size_t>(height);
    if (pixel_count > color_buffer_.max_size()) {
        return false;
    }

    SDL_Texture* replacement = SDL_CreateTexture(
        renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, width,
        height);
    if (!replacement) {
        return false;
    }
    if (texture_) {
        SDL_DestroyTexture(texture_);
    }
    texture_ = replacement;
    width_ = width;
    height_ = height;
    color_buffer_.assign(pixel_count, pixel_to_color(0));
    SDL_SetWindowSize(window_, width_ * zoom_, height_ * zoom_);
    return true;
}

void DisplayWindow::set_zoom(int zoom) {
    zoom_ = std::max(1, zoom);
    if (window_) {
        SDL_SetWindowSize(window_, width_ * zoom_, height_ * zoom_);
    }
}

void DisplayWindow::zoom_in() { set_zoom(zoom_ + 1); }

void DisplayWindow::zoom_out() { set_zoom(zoom_ - 1); }

void DisplayWindow::draw(const std::int16_t* pixels) {
    if (!pixels || !texture_ || !renderer_) {
        return;
    }
    for (std::size_t i = 0; i < color_buffer_.size(); ++i) {
        color_buffer_[i] = pixel_to_color(pixels[i]);
    }
    SDL_UpdateTexture(texture_, nullptr, color_buffer_.data(),
                      width_ * static_cast<int>(sizeof(std::uint32_t)));
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
    last_frame_draw_us_ = timing::us();
}



void DisplayWindow::handle_event(const SDL_Event& event) {
    if (event.type == SDL_QUIT ||
        (event.type == SDL_WINDOWEVENT &&
         event.window.windowID == window_id() &&
         event.window.event == SDL_WINDOWEVENT_CLOSE)) {
        open_ = false;
    }
}

Uint32 DisplayWindow::window_id() const {
    return window_ ? SDL_GetWindowID(window_) : 0U;
}
