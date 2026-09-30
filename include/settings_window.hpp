#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <iostream>
#include <filesystem>

#include <SDL2/SDL.h>

class DisplayWindow;
class EventCamera;
class EventSource;

class SettingsWindow {
public:
    SettingsWindow(DisplayWindow& raw_display, DisplayWindow& processed_display, EventCamera& camera, EventSource& event_source, std::function<void(int, int)> size_change_callback);
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    bool create(const std::string& title = "Settings",
                std::size_t columns = 3);
    void close();

    void add_binary_button(const std::string& label, bool& value,std::function<void(bool)> on_change = {},std::size_t column = 1);
    void add_binary_button(const std::string& label, bool& value,std::size_t column);
    void add_click_button(const std::string& label,std::function<void()> on_click,std::size_t column = 1);
    void add_compact_button(const std::string& label,std::function<void()> on_click,std::size_t column = 1);
    void add_int_slider(const std::string& label, int& value, int minimum,int maximum, std::function<void(int)> on_change = {},std::size_t column = 1);
    void add_int_slider(const std::string& label, int& value, int minimum,int maximum, std::size_t column);
    void add_float_slider(const std::string& label, float& value, float minimum,float maximum,std::function<void(float)> on_change = {},std::size_t column = 1);
    void add_float_slider(const std::string& label, float& value, float minimum,float maximum, std::size_t column);
    void add_int_input(const std::string& label, int& value, int minimum,int maximum, std::function<void(int)> on_change = {},std::size_t column = 1);
    void add_int_input(const std::string& label, int& value, int minimum,int maximum, std::size_t column);
    void add_float_input(const std::string& label, float& value, float minimum,float maximum,std::function<void(float)> on_change = {},std::size_t column = 1);
    void add_float_input(const std::string& label, float& value, float minimum,float maximum, std::size_t column);

    // Adds live controls for every modifiable camera bias and for hardware ROI.
    // Call after the camera has been opened.
    void add_camera_controls(std::size_t column = 2);

    void handle_event(const SDL_Event& event);
    void draw();
    bool is_open() const { return open_; }
    bool raw_persist() const { return raw_persist_; }
    bool processing_enabled() const { return processing_enabled_; }
    Uint32 window_id() const;
    std::size_t column_count() const { return column_count_; }

private:
    enum class ControlType {
        Label,
        Binary,
        Click,
        Compact,
        IntSlider,
        FloatSlider,
        IntInput,
        FloatInput
    };

    struct Control {
        ControlType type;
        std::string label;
        SDL_Rect rect{};
        std::size_t column = 0;
        bool* bool_value = nullptr;
        int* int_value = nullptr;
        float* float_value = nullptr;
        int int_min = 0;
        int int_max = 1;
        float float_min = 0.0F;
        float float_max = 1.0F;
        std::function<void(bool)> bool_callback;
        std::function<void()> click_callback;
        std::function<void(int)> int_callback;
        std::function<void(float)> float_callback;
        std::function<bool()> active_callback;
        std::uint64_t pressed_until_ms = 0;
        bool input_invalid = false;
    };

    void add_builtin_controls();
    void add_label(const std::string& label, std::size_t column);
    void relayout();
    void update_slider(Control& control, int mouse_x);
    void begin_input(Control& control);
    void commit_input();
    void end_input();
    bool apply_camera_roi();
    void set_display_resolution(int width, int height);
    void show_source_error(const std::string& action) const;
    void draw_control(const Control& control);
    void draw_text(int x, int y, const std::string& text, int scale = 2);

    DisplayWindow& raw_display_;
    DisplayWindow& processed_display_;
    EventCamera& camera_;
    EventSource& event_source_;
    std::function<void(int, int)> size_change_callback_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    std::vector<Control> controls_;
    Control* active_slider_ = nullptr;
    Control* active_input_ = nullptr;
    std::string input_text_;
    bool replace_input_text_ = false;
    int raw_zoom_value_ = 1;
    int processed_zoom_value_ = 1;
    bool raw_persist_ = false;
    bool processing_enabled_ = true;
    int replay_speed_exp_ = 0;
    float replay_speed_value_ = 1.0F;
    std::size_t column_count_ = 1;
    int window_width_ = 440;
    std::map<std::string, int> camera_bias_values_;
    int roi_x_ = 0;
    int roi_y_ = 0;
    int roi_width_ = 1;
    int roi_height_ = 1;
    bool roi_enabled_ = false;
    bool roi_local_coordinates_ = true;
    bool camera_controls_added_ = false;
    bool open_ = false;
};
