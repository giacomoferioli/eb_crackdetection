#include "settings_window.hpp"

#include "display_window.hpp"
#include "event_camera.hpp"
#include "event_source.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
constexpr int margin = 12;
constexpr int column_width = 360;
constexpr int column_gap = 12;
constexpr int row_height = 42;
constexpr int control_height = 30;
constexpr int compact_size = 42;
constexpr int compact_gap = 8;
constexpr int slider_label_width = 160;
constexpr int input_width = 132;

std::optional<std::string> run_file_dialog(const char* command) {
    FILE* pipe = popen(command, "r");
    if (!pipe) return std::nullopt;
    std::string path;
    std::array<char, 1024> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe))
        path += buffer.data();
    const int status = pclose(pipe);
    while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
        path.pop_back();
    if (status != 0 || path.empty()) return std::nullopt;
    return path;
}

std::optional<std::string> choose_png_file() {
    return run_file_dialog(
        "zenity --file-selection --title='Select an event PNG' "
        "--file-filter='PNG images | *.png *.PNG'");
}

std::optional<std::string> choose_mp4_file() {
    return run_file_dialog(
        "zenity --file-selection --save --confirm-overwrite "
        "--title='Save recorded events' --filename='recording.mp4' "
        "--file-filter='MP4 video | *.mp4 *.MP4'");
}

std::optional<std::string> choose_recording_save_file() {
    return run_file_dialog("zenity --file-selection --save --confirm-overwrite --title='Dump event recording' "
                           "--filename='recording.evbin' --file-filter='Event recordings | *.evbin'");
}

std::optional<std::string> choose_recording_load_file() {
    return run_file_dialog("zenity --file-selection --title='Load event recording' "
                           "--file-filter='Event recordings | *.evbin'");
}

bool contains(const SDL_Rect& rect, int x, int y) {
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y &&
           y < rect.y + rect.h;
}

std::string float_text(float value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << value;
    return stream.str();
}

// Compact 5x7 bitmap font keeps the settings UI independent from SDL_ttf.
std::array<std::uint8_t, 7> glyph(char input) {
    const char c = (input >= 'a' && input <= 'z') ? input - 'a' + 'A' : input;
    switch (c) {
    case 'A': return {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11};
    case 'B': return {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E};
    case 'C': return {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F};
    case 'D': return {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E};
    case 'E': return {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F};
    case 'F': return {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10};
    case 'G': return {0x0F,0x10,0x10,0x17,0x11,0x11,0x0F};
    case 'H': return {0x11,0x11,0x11,0x1F,0x11,0x11,0x11};
    case 'I': return {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E};
    case 'J': return {0x01,0x01,0x01,0x01,0x11,0x11,0x0E};
    case 'K': return {0x11,0x12,0x14,0x18,0x14,0x12,0x11};
    case 'L': return {0x10,0x10,0x10,0x10,0x10,0x10,0x1F};
    case 'M': return {0x11,0x1B,0x15,0x15,0x11,0x11,0x11};
    case 'N': return {0x11,0x19,0x15,0x13,0x11,0x11,0x11};
    case 'O': return {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E};
    case 'P': return {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10};
    case 'Q': return {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D};
    case 'R': return {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11};
    case 'S': return {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E};
    case 'T': return {0x1F,0x04,0x04,0x04,0x04,0x04,0x04};
    case 'U': return {0x11,0x11,0x11,0x11,0x11,0x11,0x0E};
    case 'V': return {0x11,0x11,0x11,0x11,0x11,0x0A,0x04};
    case 'W': return {0x11,0x11,0x11,0x15,0x15,0x15,0x0A};
    case 'X': return {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11};
    case 'Y': return {0x11,0x11,0x0A,0x04,0x04,0x04,0x04};
    case 'Z': return {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F};
    case '0': return {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E};
    case '1': return {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E};
    case '2': return {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F};
    case '3': return {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E};
    case '4': return {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02};
    case '5': return {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E};
    case '6': return {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E};
    case '7': return {0x1F,0x01,0x02,0x04,0x08,0x08,0x08};
    case '8': return {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E};
    case '9': return {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E};
    case '.': return {0,0,0,0,0,0x06,0x06};
    case '-': return {0,0,0,0x1F,0,0,0};
    case '+': return {0,0x04,0x04,0x1F,0x04,0x04,0};
    case ':': return {0,0x06,0x06,0,0x06,0x06,0};
    case '_': return {0,0,0,0,0,0,0x1F};
    default: return {0,0,0,0,0,0,0};
    }
}
} // namespace

SettingsWindow::SettingsWindow(DisplayWindow& raw_display, DisplayWindow& processed_display, EventCamera& camera, EventSource& event_source, std::function<void(int, int)> size_change_callback)
    : raw_display_(raw_display), processed_display_(processed_display), camera_(camera), event_source_(event_source), size_change_callback_(std::move(size_change_callback)) {}

SettingsWindow::~SettingsWindow() { close(); }

bool SettingsWindow::create(const std::string& title, std::size_t columns) {
    close();
    column_count_ = std::max<std::size_t>(1, columns);
    window_width_ = 2 * margin + static_cast<int>(column_count_) * column_width +
                    static_cast<int>(column_count_ - 1U) * column_gap;
    window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, window_width_, 200,
                               SDL_WINDOW_SHOWN);
    if (!window_) return false;
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_)
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer_) {
        close();
        return false;
    }
    open_ = true;
    controls_.clear();
    controls_.reserve(100);
    camera_bias_values_.clear();
    camera_controls_added_ = false;
    raw_zoom_value_ = raw_display_.zoom();
    processed_zoom_value_ = processed_display_.zoom();
    raw_persist_ = false;
    processing_enabled_ = true;
    replay_speed_exp_ = 0;
    replay_speed_value_ = event_source_.replay_speed();
    add_builtin_controls();
    return true;
}

void SettingsWindow::close() {
    end_input();
    active_slider_ = nullptr;
    controls_.clear();
    camera_bias_values_.clear();
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    renderer_ = nullptr;
    window_ = nullptr;
    open_ = false;
}

void SettingsWindow::add_builtin_controls() {
    add_label("EVENT SOURCE", 0);
    add_click_button(
        "LIVE",
        [this] {
            //on first press opens the camera and loads the settings
            if (!camera_.is_open() && camera_.open_first()){
                camera_.load_settings((std::filesystem::path("data") / "camera_settings.json").string());
                add_camera_controls();
            }       
            if (event_source_.play_live()) {
                set_display_resolution(event_source_.source_width(),
                                       event_source_.source_height());
                raw_display_.play();
                processed_display_.play();
            } else {
                show_source_error("Could not start the live feed");
            }
        },
        0);
    add_click_button(
        "REPLAY",
        [this] {
            if (event_source_.start_replay()) {
                set_display_resolution(event_source_.source_width(),
                                       event_source_.source_height());
                raw_display_.play();
                processed_display_.play();
            } else {
                show_source_error("Could not replay the clip");
            }
        },
        0);
    add_int_slider(
        "REPLAY SPEED", replay_speed_exp_, -3, 3,
        [this](int speed) { event_source_.set_replay_speed(pow(2,speed)); replay_speed_value_ = pow(2,speed); }, 0);
    add_click_button(
        "SAVE MP4",
        [this] {
            if (!event_source_.has_recording()) {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                         "Event source error",
                                         "No recorded clip is cached", window_);
                return;
            }
            // The file chooser and encoder are synchronous. Stop the hardware
            // first so its ring cannot overflow while the UI is blocked.
            event_source_.stop();
            const auto path = choose_mp4_file();
            if (!path) return;
            if (!event_source_.save_recording_mp4(*path)) {
                show_source_error("Could not save the recording");
                return;
            }
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "MP4 saved",
                                     path->c_str(), window_);
        },
        0);
    add_click_button(
        "LOAD PNG",
        [this] {
            const auto path = choose_png_file();
            if (!path) return;
            if (event_source_.load_png(*path)) {
                set_display_resolution(event_source_.source_width(),
                                       event_source_.source_height());
                raw_display_.play();
                processed_display_.play();
            } else {
                show_source_error("Could not load the PNG");
            }
        },
        0);
    add_compact_button("REC", [this] {
        if (event_source_.start_recording()) {
            set_display_resolution(event_source_.source_width(), event_source_.source_height());
            raw_display_.play();
            processed_display_.play();
        } else {
            show_source_error("Could not start recording");
        }
    }, 0);
    controls_.back().active_callback = [this] {
        return event_source_.mode() == EventSource::Mode::Recording;
    };
    add_compact_button("STOP", [this] { event_source_.stop(); }, 0);
    add_compact_button("DUMP", [this] {
        if (!event_source_.has_recording()) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Event source error", "No recorded clip is cached", window_);
            return;
        }
        event_source_.stop();
        const auto path = choose_recording_save_file();
        if (!path) return;
        if (!event_source_.save_recording_binary(*path)) show_source_error("Could not dump the recording");
    }, 0);
    add_compact_button("LOAD", [this] {
        event_source_.stop();
        const auto path = choose_recording_load_file();
        if (!path) return;
        if (event_source_.load_recording_binary(*path)) {
            set_display_resolution(event_source_.source_width(), event_source_.source_height());
        } else {
            show_source_error("Could not load the recording");
        }
    }, 0);

    add_label("DISPLAY", 1);
    add_int_slider("RAW ZOOM", raw_zoom_value_, 1, 8,
                   [this](int value) { raw_display_.set_zoom(value); }, 1);
    add_int_slider(
        "PROC ZOOM", processed_zoom_value_, 1, 8,
        [this](int value) { processed_display_.set_zoom(value); }, 1);
    add_binary_button("RAW PERSIST", raw_persist_, 1);
    add_binary_button("PROCESSING", processing_enabled_, [this](bool enabled) {
        if (enabled)
            processed_display_.show();
        else
            processed_display_.hide();
    }, 1);
    add_compact_button(
        "PLAY",
        [this] {
            // Do not replay the backlog accumulated while processing was
            // paused.
            camera_.clear_events();
            camera_.clear_trigger_events();
            raw_display_.play();
            processed_display_.play();
        },
        1);
    add_compact_button(
        "STOP",
        [this] {
            raw_display_.stop();
            processed_display_.stop();
        },
        1);
}

void SettingsWindow::add_label(const std::string& label, std::size_t column) {
    Control control{};
    control.type = ControlType::Label;
    control.label = label;
    control.column = std::min(column, column_count_ - 1U);
    controls_.push_back(std::move(control));
    relayout();
}

void SettingsWindow::add_binary_button(const std::string& label, bool& value,
                                       std::function<void(bool)> callback,
                                       std::size_t column) {
    Control control{};
    control.type = ControlType::Binary;
    control.label = label;
    control.column = std::min(column, column_count_ - 1U);
    control.bool_value = &value;
    control.bool_callback = std::move(callback);
    controls_.push_back(std::move(control));
    relayout();
}

void SettingsWindow::add_binary_button(const std::string& label, bool& value,
                                       std::size_t column) {
    add_binary_button(label, value, {}, column);
}

void SettingsWindow::add_click_button(const std::string& label,
                                      std::function<void()> callback,
                                      std::size_t column) {
    Control control{};
    control.type = ControlType::Click;
    control.label = label;
    control.column = std::min(column, column_count_ - 1U);
    control.click_callback = std::move(callback);
    controls_.push_back(std::move(control));
    relayout();
}

void SettingsWindow::add_compact_button(const std::string& label,
                                        std::function<void()> callback,
                                        std::size_t column) {
    Control control{};
    control.type = ControlType::Compact;
    control.label = label;
    control.column = std::min(column, column_count_ - 1U);
    control.click_callback = std::move(callback);
    controls_.push_back(std::move(control));
    relayout();
}

void SettingsWindow::add_int_slider(const std::string& label, int& value,
                                    int minimum, int maximum,
                                    std::function<void(int)> callback,
                                    std::size_t column) {
    if (minimum > maximum) std::swap(minimum, maximum);
    value = std::clamp(value, minimum, maximum);
    Control control{};
    control.type = ControlType::IntSlider;
    control.label = label;
    control.column = std::min(column, column_count_ - 1U);
    control.int_value = &value;
    control.int_min = minimum;
    control.int_max = maximum;
    control.int_callback = std::move(callback);
    controls_.push_back(std::move(control));
    relayout();
}

void SettingsWindow::add_int_slider(const std::string& label, int& value,
                                    int minimum, int maximum,
                                    std::size_t column) {
    add_int_slider(label, value, minimum, maximum, {}, column);
}

void SettingsWindow::add_float_slider(const std::string& label, float& value,
                                      float minimum, float maximum,
                                      std::function<void(float)> callback,
                                      std::size_t column) {
    if (minimum > maximum) std::swap(minimum, maximum);
    value = std::clamp(value, minimum, maximum);
    Control control{};
    control.type = ControlType::FloatSlider;
    control.label = label;
    control.column = std::min(column, column_count_ - 1U);
    control.float_value = &value;
    control.float_min = minimum;
    control.float_max = maximum;
    control.float_callback = std::move(callback);
    controls_.push_back(std::move(control));
    relayout();
}

void SettingsWindow::add_float_slider(const std::string& label, float& value,
                                      float minimum, float maximum,
                                      std::size_t column) {
    add_float_slider(label, value, minimum, maximum, {}, column);
}

void SettingsWindow::add_int_input(const std::string& label, int& value,
                                   int minimum, int maximum,
                                   std::function<void(int)> callback,
                                   std::size_t column) {
    if (minimum > maximum) std::swap(minimum, maximum);
    value = std::clamp(value, minimum, maximum);
    Control control{};
    control.type = ControlType::IntInput;
    control.label = label;
    control.column = std::min(column, column_count_ - 1U);
    control.int_value = &value;
    control.int_min = minimum;
    control.int_max = maximum;
    control.int_callback = std::move(callback);
    controls_.push_back(std::move(control));
    relayout();
}

void SettingsWindow::add_int_input(const std::string& label, int& value,
                                   int minimum, int maximum,
                                   std::size_t column) {
    add_int_input(label, value, minimum, maximum, {}, column);
}

void SettingsWindow::add_float_input(const std::string& label, float& value,
                                     float minimum, float maximum,
                                     std::function<void(float)> callback,
                                     std::size_t column) {
    if (minimum > maximum) std::swap(minimum, maximum);
    value = std::clamp(value, minimum, maximum);
    Control control{};
    control.type = ControlType::FloatInput;
    control.label = label;
    control.column = std::min(column, column_count_ - 1U);
    control.float_value = &value;
    control.float_min = minimum;
    control.float_max = maximum;
    control.float_callback = std::move(callback);
    controls_.push_back(std::move(control));
    relayout();
}

void SettingsWindow::add_float_input(const std::string& label, float& value,
                                     float minimum, float maximum,
                                     std::size_t column) {
    add_float_input(label, value, minimum, maximum, {}, column);
}

void SettingsWindow::add_camera_controls(std::size_t column) {
    if (camera_controls_added_) return;
    column = std::min(column, column_count_ - 1U);
    if (!camera_.is_open()) {
        add_label("CAMERA OFFLINE", column);
        return;
    }
    camera_controls_added_ = true;
    add_label("CAMERA", column);

    for (const EventCamera::BiasSetting& setting : camera_.bias_settings()) {
        if (!setting.modifiable) continue;
        auto [iterator, inserted] =
            camera_bias_values_.emplace(setting.name, setting.value);
        static_cast<void>(inserted);
        int& value = iterator->second;
        add_int_input(
            setting.name, value, setting.minimum, setting.maximum,
            [this, name = setting.name](int requested) {
                if (camera_.set_bias(name, requested)) {
                    if (const auto actual = camera_.bias(name))
                        camera_bias_values_[name] = *actual;
                } else if (const auto actual = camera_.bias(name)) {
                    camera_bias_values_[name] = *actual;
                }
            },
            column);
    }

    add_label("ROI", column);
    if (const auto saved_roi = camera_.roi_setting()) {
        roi_x_ = saved_roi->x;
        roi_y_ = saved_roi->y;
        roi_width_ = saved_roi->width;
        roi_height_ = saved_roi->height;
        roi_enabled_ = saved_roi->enabled;
    } else {
        roi_x_ = 0;
        roi_y_ = 0;
        roi_width_ = std::max(1, camera_.width());
        roi_height_ = std::max(1, camera_.height());
        roi_enabled_ = false;
    }
    roi_local_coordinates_ = true;
    camera_.set_roi_coordinates_local(true);
    if (roi_enabled_ && event_source_.uses_camera())
        set_display_resolution(roi_width_, roi_height_);
    const auto apply_changed_roi = [this](int) {
        roi_enabled_ = apply_camera_roi();
    };
    add_int_input("ROI X", roi_x_, 0, std::max(0, camera_.width() - 1),
                  apply_changed_roi, column);
    add_int_input("ROI Y", roi_y_, 0, std::max(0, camera_.height() - 1),
                  apply_changed_roi, column);
    add_int_input("ROI WIDTH", roi_width_, 1, std::max(1, camera_.width()),
                  apply_changed_roi, column);
    add_int_input("ROI HEIGHT", roi_height_, 1,
                  std::max(1, camera_.height()), apply_changed_roi, column);
    add_binary_button(
        "ROI ENABLE", roi_enabled_,
        [this](bool enabled) {
            if (enabled) {
                roi_enabled_ = apply_camera_roi();
            } else if (camera_.enable_roi(false)) {
                if (event_source_.uses_camera())
                    set_display_resolution(camera_.width(), camera_.height());
            } else {
                roi_enabled_ = true;
            }
        },
        column);
    add_binary_button(
        "ROI LOCAL XY", roi_local_coordinates_,
        [this](bool enabled) {
            camera_.set_roi_coordinates_local(enabled);
            if (roi_enabled_ && event_source_.uses_camera()) {
                if (enabled)
                    set_display_resolution(roi_width_, roi_height_);
                else
                    set_display_resolution(camera_.width(), camera_.height());
            }
        },
        column);
    add_compact_button(
        "FULL",
        [this] {
            roi_x_ = 0;
            roi_y_ = 0;
            roi_width_ = std::max(1, camera_.width());
            roi_height_ = std::max(1, camera_.height());
            roi_enabled_ = apply_camera_roi();
        },
        column);
}

bool SettingsWindow::apply_camera_roi() {
    if (!camera_.is_open()) return false;
    roi_x_ = std::clamp(roi_x_, 0, std::max(0, camera_.width() - 1));
    roi_y_ = std::clamp(roi_y_, 0, std::max(0, camera_.height() - 1));
    roi_width_ = std::clamp(roi_width_, 1, camera_.width() - roi_x_);
    roi_height_ = std::clamp(roi_height_, 1, camera_.height() - roi_y_);
    const bool success =
        camera_.set_roi(roi_x_, roi_y_, roi_width_, roi_height_);
    if (success && event_source_.uses_camera()) {
        if (roi_local_coordinates_)
            set_display_resolution(roi_width_, roi_height_);
        else
            set_display_resolution(camera_.width(), camera_.height());
    }
    return success;
}

void SettingsWindow::set_display_resolution(int width, int height) {
    raw_display_.set_resolution(width, height);
    processed_display_.set_resolution(width, height);
    size_change_callback_(width, height);
}

void SettingsWindow::show_source_error(const std::string& action) const {
    const std::string message = action + ":\n" + event_source_.last_error();
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Event source error",
                             message.c_str(), window_);
}

void SettingsWindow::relayout() {
    std::vector<int> normal_rows(column_count_, 0);
    std::vector<int> compact_counts(column_count_, 0);
    for (const Control& control : controls_) {
        if (control.type == ControlType::Compact)
            ++compact_counts[control.column];
        else
            ++normal_rows[control.column];
    }
    const int max_normal =
        *std::max_element(normal_rows.begin(), normal_rows.end());
    const int compact_per_row =
        std::max(1, (column_width + compact_gap) / (compact_size + compact_gap));
    int max_compact_rows = 0;
    for (int count : compact_counts) {
        max_compact_rows =
            std::max(max_compact_rows,
                     (count + compact_per_row - 1) / compact_per_row);
    }
    const int compact_top = margin + max_normal * row_height +
                            (max_compact_rows > 0 ? margin : 0);

    std::fill(normal_rows.begin(), normal_rows.end(), 0);
    std::fill(compact_counts.begin(), compact_counts.end(), 0);
    for (Control& control : controls_) {
        const int column_x = margin + static_cast<int>(control.column) *
                                          (column_width + column_gap);
        if (control.type == ControlType::Compact) {
            const int index = compact_counts[control.column]++;
            control.rect = {column_x + (index % compact_per_row) *
                                            (compact_size + compact_gap),
                            compact_top + (index / compact_per_row) *
                                              (compact_size + compact_gap),
                            compact_size, compact_size};
        } else {
            const int row = normal_rows[control.column]++;
            control.rect = {column_x, margin + row * row_height, column_width,
                            control_height};
        }
    }

    const int compact_height = max_compact_rows == 0
                                   ? 0
                                   : margin + max_compact_rows *
                                                  (compact_size + compact_gap);
    const int height =
        std::max(120, 2 * margin + max_normal * row_height + compact_height);
    if (window_) SDL_SetWindowSize(window_, window_width_, height);
}

void SettingsWindow::handle_event(const SDL_Event& event) {
    if (event.type == SDL_QUIT ||
        (event.type == SDL_WINDOWEVENT && event.window.windowID == window_id() &&
         event.window.event == SDL_WINDOWEVENT_CLOSE)) {
        open_ = false;
        end_input();
        return;
    }

    if (event.type == SDL_TEXTINPUT && active_input_ &&
        event.text.windowID == window_id()) {
        for (const char* character = event.text.text; *character; ++character) {
            const bool digit = *character >= '0' && *character <= '9';
            const bool integer_symbol = *character == '-' || *character == '+';
            const bool float_symbol = *character == '.' || *character == 'e' ||
                                      *character == 'E';
            if (!digit && !integer_symbol &&
                !(active_input_->type == ControlType::FloatInput && float_symbol))
                continue;
            if (replace_input_text_) {
                input_text_.clear();
                replace_input_text_ = false;
            }
            if (input_text_.size() < 16) input_text_.push_back(*character);
            active_input_->input_invalid = false;
        }
        return;
    }

    if (event.type == SDL_KEYDOWN && active_input_ &&
        event.key.windowID == window_id()) {
        if (event.key.keysym.sym == SDLK_RETURN ||
            event.key.keysym.sym == SDLK_KP_ENTER) {
            commit_input();
        } else if (event.key.keysym.sym == SDLK_ESCAPE) {
            end_input();
        } else if (event.key.keysym.sym == SDLK_BACKSPACE) {
            if (replace_input_text_) {
                input_text_.clear();
                replace_input_text_ = false;
            } else if (!input_text_.empty()) {
                input_text_.pop_back();
            }
            active_input_->input_invalid = false;
        }
        return;
    }

    if (event.type == SDL_MOUSEBUTTONDOWN &&
        event.button.windowID == window_id() &&
        event.button.button == SDL_BUTTON_LEFT) {
        Control* hit = nullptr;
        for (Control& control : controls_) {
            if (contains(control.rect, event.button.x, event.button.y)) {
                hit = &control;
                break;
            }
        }
        if (active_input_ && hit != active_input_) end_input();
        if (!hit) return;

        if (hit->type == ControlType::Binary) {
            *hit->bool_value = !*hit->bool_value;
            hit->pressed_until_ms = SDL_GetTicks64() + 180U;
            draw();
            if (hit->bool_callback) hit->bool_callback(*hit->bool_value);
        } else if (hit->type == ControlType::Click ||
                   hit->type == ControlType::Compact) {
            hit->pressed_until_ms = SDL_GetTicks64() + 180U;
            draw();
            if (hit->click_callback) hit->click_callback();
        } else if (hit->type == ControlType::IntSlider ||
                   hit->type == ControlType::FloatSlider) {
            active_slider_ = hit;
            update_slider(*hit, event.button.x);
        } else if (hit->type == ControlType::IntInput ||
                   hit->type == ControlType::FloatInput) {
            begin_input(*hit);
        }
    } else if (event.type == SDL_MOUSEMOTION && active_slider_ &&
               event.motion.windowID == window_id() &&
               (event.motion.state & SDL_BUTTON_LMASK)) {
        update_slider(*active_slider_, event.motion.x);
    } else if (event.type == SDL_MOUSEBUTTONUP &&
               event.button.button == SDL_BUTTON_LEFT) {
        active_slider_ = nullptr;
    }
}

void SettingsWindow::update_slider(Control& control, int mouse_x) {
    const int start = control.rect.x + slider_label_width;
    const int length = control.rect.w - slider_label_width - 12;
    const float fraction = std::clamp(
        static_cast<float>(mouse_x - start) / static_cast<float>(length), 0.0F,
        1.0F);
    if (control.type == ControlType::IntSlider) {
        *control.int_value = static_cast<int>(std::lround(
            control.int_min + fraction * (control.int_max - control.int_min)));
        if (control.int_callback) control.int_callback(*control.int_value);
    } else {
        *control.float_value = control.float_min +
                               fraction * (control.float_max - control.float_min);
        if (control.float_callback) control.float_callback(*control.float_value);
    }
}

void SettingsWindow::begin_input(Control& control) {
    if (active_input_ && active_input_ != &control) end_input();
    active_input_ = &control;
    active_input_->input_invalid = false;
    input_text_ = control.type == ControlType::IntInput
                      ? std::to_string(*control.int_value)
                      : float_text(*control.float_value);
    replace_input_text_ = true;
    SDL_StartTextInput();
}

void SettingsWindow::commit_input() {
    if (!active_input_) return;
    try {
        std::size_t parsed = 0;
        if (active_input_->type == ControlType::IntInput) {
            const long parsed_value = std::stol(input_text_, &parsed);
            if (parsed != input_text_.size() ||
                parsed_value < std::numeric_limits<int>::min() ||
                parsed_value > std::numeric_limits<int>::max())
                throw std::invalid_argument("invalid integer");
            *active_input_->int_value = std::clamp(
                static_cast<int>(parsed_value), active_input_->int_min,
                active_input_->int_max);
            if (active_input_->int_callback)
                active_input_->int_callback(*active_input_->int_value);
        } else {
            const float parsed_value = std::stof(input_text_, &parsed);
            if (parsed != input_text_.size() || !std::isfinite(parsed_value))
                throw std::invalid_argument("invalid float");
            *active_input_->float_value =
                std::clamp(parsed_value, active_input_->float_min,
                           active_input_->float_max);
            if (active_input_->float_callback)
                active_input_->float_callback(*active_input_->float_value);
        }
        active_input_->input_invalid = false;
        end_input();
    } catch (const std::exception&) {
        active_input_->input_invalid = true;
        replace_input_text_ = false;
    }
}

void SettingsWindow::end_input() {
    if (active_input_) {
        active_input_->input_invalid = false;
        SDL_StopTextInput();
    }
    active_input_ = nullptr;
    input_text_.clear();
    replace_input_text_ = false;
}

void SettingsWindow::draw() {
    if (!renderer_) return;
    SDL_SetRenderDrawColor(renderer_, 28, 31, 38, 255);
    SDL_RenderClear(renderer_);
    SDL_SetRenderDrawColor(renderer_, 70, 74, 84, 255);
    for (std::size_t column = 1; column < column_count_; ++column) {
        const int x = margin + static_cast<int>(column) *
                                  (column_width + column_gap) -
                      column_gap / 2;
        SDL_RenderDrawLine(renderer_, x, margin, x, 2000);
    }
    for (const Control& control : controls_) draw_control(control);
    SDL_RenderPresent(renderer_);
}

void SettingsWindow::draw_control(const Control& control) {
    if (control.type == ControlType::Label) {
        draw_text(control.rect.x + 4, control.rect.y + 7, control.label, 2);
        SDL_SetRenderDrawColor(renderer_, 76, 155, 230, 255);
        SDL_RenderDrawLine(renderer_, control.rect.x, control.rect.y + 27,
                           control.rect.x + control.rect.w,
                           control.rect.y + 27);
        return;
    }

    const bool pressed = control.pressed_until_ms > SDL_GetTicks64();
    const bool active = control.active_callback && control.active_callback();
    if (active)
        SDL_SetRenderDrawColor(renderer_, 125, 35, 42, 255);
    else if (pressed)
        SDL_SetRenderDrawColor(renderer_, 76, 125, 185, 255);
    else
        SDL_SetRenderDrawColor(renderer_, 54, 59, 70, 255);
    SDL_RenderFillRect(renderer_, &control.rect);

    if (control.type == ControlType::Compact) {
        if (active)
            SDL_SetRenderDrawColor(renderer_, 255, 75, 82, 255);
        else if (pressed)
            SDL_SetRenderDrawColor(renderer_, 145, 195, 255, 255);
        else
            SDL_SetRenderDrawColor(renderer_, 68, 110, 190, 255);
        SDL_RenderDrawRect(renderer_, &control.rect);
        const int text_width = static_cast<int>(control.label.size()) * 6;
        const int offset = pressed ? 1 : 0;
        draw_text(control.rect.x + (control.rect.w - text_width) / 2 + offset,
                  control.rect.y + 17 + offset, control.label, 1);
        return;
    }

    if (control.type == ControlType::IntInput ||
        control.type == ControlType::FloatInput) {
        draw_text(control.rect.x + 8, control.rect.y + 8, control.label, 2);
        SDL_Rect field{control.rect.x + control.rect.w - input_width,
                       control.rect.y + 3, input_width - 5,
                       control.rect.h - 6};
        SDL_SetRenderDrawColor(renderer_, 23, 26, 32, 255);
        SDL_RenderFillRect(renderer_, &field);
        if (control.input_invalid)
            SDL_SetRenderDrawColor(renderer_, 220, 75, 75, 255);
        else if (&control == active_input_)
            SDL_SetRenderDrawColor(renderer_, 76, 155, 230, 255);
        else
            SDL_SetRenderDrawColor(renderer_, 115, 120, 132, 255);
        SDL_RenderDrawRect(renderer_, &field);
        const std::string value =
            &control == active_input_
                ? input_text_
                : (control.type == ControlType::IntInput
                       ? std::to_string(*control.int_value)
                       : float_text(*control.float_value));
        draw_text(field.x + 6, field.y + 5, value, 2);
        if (&control == active_input_) {
            const int cursor_x = field.x + 6 + static_cast<int>(value.size()) * 12;
            SDL_SetRenderDrawColor(renderer_, 235, 238, 243, 255);
            SDL_RenderDrawLine(renderer_, cursor_x, field.y + 4, cursor_x,
                               field.y + field.h - 4);
        }
        return;
    }

    std::string caption = control.label;
    if (control.type == ControlType::IntSlider)
        caption += ":" + std::to_string(*control.int_value);
    else if (control.type == ControlType::FloatSlider)
        caption += ":" + float_text(*control.float_value);
    const int press_offset = pressed ? 1 : 0;
    draw_text(control.rect.x + 8 + press_offset,
              control.rect.y + 8 + press_offset, caption, 2);

    if (control.type == ControlType::Binary) {
        SDL_Rect indicator{control.rect.x + control.rect.w - 56,
                           control.rect.y + 5, 48, 20};
        if (pressed)
            SDL_SetRenderDrawColor(renderer_, 145, 195, 255, 255);
        else if (*control.bool_value)
            SDL_SetRenderDrawColor(renderer_, 52, 190, 110, 255);
        else
            SDL_SetRenderDrawColor(renderer_, 125, 130, 140, 255);
        SDL_RenderFillRect(renderer_, &indicator);
        draw_text(indicator.x + 5, indicator.y + 3,
                  *control.bool_value ? "ON" : "OFF", 2);
        return;
    }
    if (control.type == ControlType::Click) {
        if (pressed)
            SDL_SetRenderDrawColor(renderer_, 145, 195, 255, 255);
        else
            SDL_SetRenderDrawColor(renderer_, 68, 110, 190, 255);
        SDL_RenderDrawRect(renderer_, &control.rect);
        return;
    }

    SDL_Rect track{control.rect.x + slider_label_width, control.rect.y + 13,
                   control.rect.w - slider_label_width - 12, 4};
    SDL_SetRenderDrawColor(renderer_, 115, 120, 132, 255);
    SDL_RenderFillRect(renderer_, &track);
    float fraction = 0.0F;
    if (control.type == ControlType::IntSlider &&
        control.int_max != control.int_min) {
        fraction = static_cast<float>(*control.int_value - control.int_min) /
                   static_cast<float>(control.int_max - control.int_min);
    } else if (control.type == ControlType::FloatSlider &&
               control.float_max != control.float_min) {
        fraction = (*control.float_value - control.float_min) /
                   (control.float_max - control.float_min);
    }
    SDL_Rect knob{track.x + static_cast<int>(fraction * track.w) - 5,
                  control.rect.y + 6, 10, 18};
    SDL_SetRenderDrawColor(renderer_, 76, 155, 230, 255);
    SDL_RenderFillRect(renderer_, &knob);
}

void SettingsWindow::draw_text(int x, int y, const std::string& text,
                               int scale) {
    SDL_SetRenderDrawColor(renderer_, 235, 238, 243, 255);
    for (char character : text) {
        const auto rows = glyph(character);
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if (rows[row] & (1U << (4 - column))) {
                    SDL_Rect pixel{x + column * scale, y + row * scale, scale,
                                   scale};
                    SDL_RenderFillRect(renderer_, &pixel);
                }
            }
        }
        x += 6 * scale;
    }
}

Uint32 SettingsWindow::window_id() const {
    return window_ ? SDL_GetWindowID(window_) : 0U;
}
