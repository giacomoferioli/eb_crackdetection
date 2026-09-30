#pragma once

#include "event_camera.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Selects where Processing's event batch comes from. Processing always sees
// the same vector<EventCamera::Event>, independently of the active source.
class EventSource {
public:
    enum class Mode { Live, Recording, Stopped, Replay, Image };

    explicit EventSource(EventCamera& camera);

    EventSource(const EventSource&) = delete;
    EventSource& operator=(const EventSource&) = delete;

    bool play_live();
    bool start_recording();
    void stop();
    bool start_replay();

    void set_replay_speed(float speed);
    float replay_speed() const { return replay_speed_; }

    bool load_png(const std::string& path);
    bool save_recording_mp4(const std::string& path);
    bool save_recording_binary(const std::string& path);
    bool load_recording_binary(const std::string& path);

    std::size_t read_events(std::vector<EventCamera::Event>& output, size_t max_events = 0);

    Mode mode() const { return mode_; }
    int source_width() const { return source_width_; }
    int source_height() const { return source_height_; }
    bool has_recording() const { return !recorded_events_.empty(); }
    bool uses_camera() const {
        return mode_ == Mode::Live || mode_ == Mode::Recording;
    }
    std::size_t recorded_event_count() const { return recorded_events_.size(); }
    const std::string& last_error() const { return last_error_; }

private:
    bool ensure_camera_running();
    void update_camera_dimensions();
    std::size_t read_replay(std::vector<EventCamera::Event>& output);
    std::size_t read_image(std::vector<EventCamera::Event>& output);
    void set_error(std::string message);

    EventCamera& camera_;
    Mode mode_ = Mode::Live;
    std::vector<EventCamera::Event> recorded_events_;
    std::vector<EventCamera::Event> image_events_;
    std::size_t replay_cursor_ = 0;
    std::uint64_t replay_last_wall_us_ = 0;
    double replay_position_us_ = 0.0;
    float replay_speed_ = 1.0F;
    int source_width_ = 0;
    int source_height_ = 0;
    int recording_width_ = 0;
    int recording_height_ = 0;
    std::string last_error_;
};
