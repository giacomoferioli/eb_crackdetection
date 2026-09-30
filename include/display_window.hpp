#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

#include <SDL2/SDL.h>

class DisplayWindow {
public:
    DisplayWindow();
    ~DisplayWindow();

    DisplayWindow(const DisplayWindow&) = delete;
    DisplayWindow& operator=(const DisplayWindow&) = delete;

    bool create(const std::string& title, int width, int height);
    void close();
    void show();
    void hide();

    bool set_resolution(int width, int height);
    void set_zoom(int zoom);
    void zoom_in();
    void zoom_out();
    int zoom() const { return zoom_; }

    void draw(const std::int16_t* pixels);

    void play() { playing_ = true; }
    void stop() { playing_ = false; }
    bool is_playing() const { return playing_; }

    void handle_event(const SDL_Event& event);
    bool is_open() const { return open_; }
    int width() const { return width_; }
    int height() const { return height_; }
    Uint32 window_id() const;

    void set_color_function(std::function<std::uint32_t (int16_t)> fun){ this->pixel_to_color= fun;};

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    std::vector<std::uint32_t> color_buffer_;
    int width_ = 0;
    int height_ = 0;
    int zoom_ = 1;
    mutable std::uint64_t last_frame_draw_us_ = 0;
    bool playing_ = true;
    bool open_ = false;

    std::function<std::uint32_t (int16_t)> pixel_to_color;
};
