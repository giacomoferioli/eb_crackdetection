#include "event_source.hpp"

#include "timing.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <utility>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

namespace {
constexpr std::size_t maximum_recorded_events = 1U << 24U;
constexpr double export_fps = 30.0;
constexpr std::array<char, 8> recording_magic{'E', 'V', 'R', 'E', 'C', '0', '1', '\0'};
constexpr std::uint32_t recording_version = 1;
constexpr std::uintmax_t recording_header_size = 32;

template<typename T>
bool write_binary(std::ofstream& output, const T& value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    return static_cast<bool>(output);
}

template<typename T>
bool read_binary(std::ifstream& input, T& value) {
    input.read(reinterpret_cast<char*>(&value), sizeof(value));
    return static_cast<bool>(input);
}
}

EventSource::EventSource(EventCamera& camera) : camera_(camera) {}

void EventSource::update_camera_dimensions() {
    const auto roi = camera_.roi_setting();
    if (roi && roi->enabled && camera_.roi_coordinates_local()) {
        source_width_ = roi->width;
        source_height_ = roi->height;
    } else {
        source_width_ = camera_.width();
        source_height_ = camera_.height();
    }
}

bool EventSource::ensure_camera_running() {
    if (!camera_.is_open()) {
        set_error("No event camera is open");
        return false;
    }
    if (!camera_.is_running() && !camera_.start()) {
        set_error(camera_.last_error());
        return false;
    }
    return true;
}

bool EventSource::play_live() {
    if (!ensure_camera_running()) return false;
    camera_.clear_events();
    camera_.clear_trigger_events();
    mode_ = Mode::Live;
    update_camera_dimensions();
    last_error_.clear();
    return true;
}

bool EventSource::start_recording() {
    if (!ensure_camera_running()) return false;
    update_camera_dimensions();
    if (source_width_ <= 0 || source_height_ <= 0) {
        set_error("The recording dimensions are invalid");
        return false;
    }

    camera_.clear_events();
    camera_.clear_trigger_events();
    recorded_events_.clear();
    try {
        recorded_events_.reserve(1U << 22U);
    } catch (const std::bad_alloc&) {
        set_error("Could not allocate the recording cache");
        return false;
    }
    recording_width_ = source_width_;
    recording_height_ = source_height_;
    mode_ = Mode::Recording;
    last_error_.clear();
    return true;
}

void EventSource::stop() {
    if (camera_.is_running()) camera_.stop();
    mode_ = Mode::Stopped;
}

bool EventSource::start_replay() {
    if (recorded_events_.empty()) {
        set_error("No recorded clip is cached");
        return false;
    }
    if (camera_.is_running()) camera_.stop();
    replay_cursor_ = 0;
    replay_position_us_ = 0.0;
    replay_last_wall_us_ = timing::us();
    source_width_ = recording_width_;
    source_height_ = recording_height_;
    mode_ = Mode::Replay;
    last_error_.clear();
    return true;
}

void EventSource::set_replay_speed(float speed) {
    if (std::isfinite(speed)) replay_speed_ = std::clamp(speed, 0.1F, 32.0F);
}

bool EventSource::load_png(const std::string& path) {
    const cv::Mat image = cv::imread(path, cv::IMREAD_COLOR);
    if (image.empty()) {
        set_error("Could not open the selected PNG file");
        return false;
    }
    if (image.cols > std::numeric_limits<std::uint16_t>::max() ||
        image.rows > std::numeric_limits<std::uint16_t>::max()) {
        set_error("The PNG dimensions exceed the event coordinate range");
        return false;
    }

    std::vector<EventCamera::Event> replacement;
    try {
        replacement.reserve(static_cast<std::size_t>(image.cols) * image.rows);
        for (int y = 0; y < image.rows; ++y) {
            const cv::Vec3b* row = image.ptr<cv::Vec3b>(y);
            for (int x = 0; x < image.cols; ++x) {
                const bool black = row[x][0] == 0 && row[x][1] == 0 &&
                                   row[x][2] == 0;
                const bool white = row[x][0] == 255 && row[x][1] == 255 &&
                                   row[x][2] == 255;
                if (black || white) {
                    replacement.push_back(
                        {0, static_cast<std::uint16_t>(x),
                         static_cast<std::uint16_t>(y),
                         static_cast<std::uint8_t>(white), {0, 0, 0}});
                }
            }
        }
    } catch (const std::bad_alloc&) {
        set_error("Could not allocate events for the selected PNG");
        return false;
    }

    if (camera_.is_running()) camera_.stop();
    image_events_ = std::move(replacement);
    source_width_ = image.cols;
    source_height_ = image.rows;
    mode_ = Mode::Image;
    last_error_.clear();
    return true;
}

bool EventSource::save_recording_mp4(const std::string& path) {
    if (recorded_events_.empty() || recording_width_ <= 0 ||
        recording_height_ <= 0) {
        set_error("No recorded clip is cached");
        return false;
    }

    std::filesystem::path output_path(path);
    if (output_path.extension() != ".mp4") output_path += ".mp4";
    std::error_code error;
    if (output_path.has_parent_path())
        std::filesystem::create_directories(output_path.parent_path(), error);
    if (error) {
        set_error("Could not create the MP4 output directory");
        return false;
    }

    cv::VideoWriter writer(
        output_path.string(), cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
        export_fps, cv::Size(recording_width_, recording_height_), true);
    if (!writer.isOpened()) {
        set_error("Could not open an MP4 encoder for the selected file");
        return false;
    }

    const std::int64_t first_timestamp = recorded_events_.front().timestamp_us;
    const std::int64_t last_timestamp = recorded_events_.back().timestamp_us;
    const double source_step_us = 1'000'000.0 / export_fps * replay_speed_;
    const std::size_t frame_count = std::max<std::size_t>(
        1, static_cast<std::size_t>(
               std::ceil(std::max<std::int64_t>(0, last_timestamp -
                                                       first_timestamp) /
                         source_step_us)));

    std::size_t cursor = 0;
    for (std::size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
        cv::Mat frame(recording_height_, recording_width_, CV_8UC3,
                      cv::Scalar(127, 127, 127));
        const auto deadline = static_cast<std::int64_t>(
            first_timestamp + (frame_index + 1) * source_step_us);
        while (cursor < recorded_events_.size() &&
               recorded_events_[cursor].timestamp_us <= deadline) {
            const EventCamera::Event& event = recorded_events_[cursor++];
            if (event.x < recording_width_ && event.y < recording_height_) {
                const std::uint8_t value = event.polarity ? 255U : 0U;
                frame.at<cv::Vec3b>(event.y, event.x) =
                    cv::Vec3b(value, value, value);
            }
        }
        writer.write(frame);
    }
    writer.release();
    last_error_.clear();
    return true;
}

bool EventSource::save_recording_binary(const std::string& path) {
    if (recorded_events_.empty() || recording_width_ <= 0 || recording_height_ <= 0) {
        set_error("No recorded clip is cached");
        return false;
    }

    std::filesystem::path output_path(path);
    if (output_path.extension() != ".evbin") output_path += ".evbin";
    std::error_code error;
    if (output_path.has_parent_path()) std::filesystem::create_directories(output_path.parent_path(), error);
    if (error) {
        set_error("Could not create the recording output directory");
        return false;
    }

    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    const std::uint32_t width = static_cast<std::uint32_t>(recording_width_);
    const std::uint32_t height = static_cast<std::uint32_t>(recording_height_);
    const std::uint32_t event_size = sizeof(EventCamera::Event);
    const std::uint64_t event_count = recorded_events_.size();
    output.write(recording_magic.data(), recording_magic.size());
    if (!output || !write_binary(output, recording_version) || !write_binary(output, width) || !write_binary(output, height) ||
        !write_binary(output, event_size) || !write_binary(output, event_count)) {
        set_error("Could not write the recording header");
        return false;
    }
    output.write(reinterpret_cast<const char*>(recorded_events_.data()), static_cast<std::streamsize>(recorded_events_.size() * sizeof(EventCamera::Event)));
    if (!output) {
        set_error("Could not write all recorded events");
        return false;
    }
    last_error_.clear();
    return true;
}

bool EventSource::load_recording_binary(const std::string& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        set_error("Could not open the recording file");
        return false;
    }
    const std::streampos file_end = input.tellg();
    if (file_end < 0) {
        set_error("Could not read the recording file size");
        return false;
    }
    const std::uintmax_t file_size = static_cast<std::uintmax_t>(file_end);
    input.seekg(0);

    std::array<char, 8> magic{};
    std::uint32_t version = 0, width = 0, height = 0, event_size = 0;
    std::uint64_t event_count = 0;
    input.read(magic.data(), magic.size());
    if (!input || !read_binary(input, version) || !read_binary(input, width) || !read_binary(input, height) ||
        !read_binary(input, event_size) || !read_binary(input, event_count)) {
        set_error("The recording header is incomplete");
        return false;
    }
    if (magic != recording_magic || version != recording_version || event_size != sizeof(EventCamera::Event)) {
        set_error("The selected file is not a supported event recording");
        return false;
    }
    if (width == 0 || height == 0 || width > 65'536U || height > 65'536U || event_count == 0 ||
        event_count > maximum_recorded_events) {
        set_error("The recording dimensions or event count are invalid");
        return false;
    }
    const std::uintmax_t expected_size = recording_header_size + event_count * sizeof(EventCamera::Event);
    if (file_size != expected_size) {
        set_error("The recording file size does not match its header");
        return false;
    }

    std::vector<EventCamera::Event> replacement;
    try {
        replacement.resize(static_cast<std::size_t>(event_count));
    } catch (const std::bad_alloc&) {
        set_error("Could not allocate the recording cache");
        return false;
    }
    input.read(reinterpret_cast<char*>(replacement.data()), static_cast<std::streamsize>(replacement.size() * sizeof(EventCamera::Event)));
    if (!input) {
        set_error("The recording event data is incomplete");
        return false;
    }
    for (std::size_t i = 0; i < replacement.size(); ++i) {
        const EventCamera::Event& event = replacement[i];
        if (event.x >= width || event.y >= height || event.polarity > 1 ||
            (i > 0 && event.timestamp_us < replacement[i - 1].timestamp_us)) {
            set_error("The recording contains invalid event data");
            return false;
        }
    }

    if (camera_.is_running()) camera_.stop();
    recorded_events_ = std::move(replacement);
    recording_width_ = static_cast<int>(width);
    recording_height_ = static_cast<int>(height);
    source_width_ = recording_width_;
    source_height_ = recording_height_;
    replay_cursor_ = 0;
    mode_ = Mode::Stopped;
    last_error_.clear();
    return true;
}

std::size_t EventSource::read_events(std::vector<EventCamera::Event>& output,size_t max_events) {
    if (mode_ == Mode::Replay) return read_replay(output);
    if (mode_ == Mode::Image) return read_image(output);
    if (mode_ == Mode::Stopped) {
        output.clear();
        return 0;
    }

    const std::size_t count = camera_.read_events(output,max_events);
    if (mode_ != Mode::Recording || count == 0) return count;

    const std::size_t available = maximum_recorded_events -
                                  std::min(maximum_recorded_events,
                                           recorded_events_.size());
    const std::size_t appended = std::min(available, count);
    try {
        recorded_events_.insert(recorded_events_.end(), output.begin(),
                                output.begin() + appended);
    } catch (const std::bad_alloc&) {
        mode_ = Mode::Live;
        set_error("Recording stopped because the cache is out of memory");
        return count;
    }
    if (appended != count) {
        mode_ = Mode::Live;
        set_error("Recording cache reached its 16-million-event limit");
    }
    return count;
}

std::size_t EventSource::read_replay(
    std::vector<EventCamera::Event>& output) {
    const std::uint64_t now = timing::us();
    replay_position_us_ +=
        static_cast<double>(now - replay_last_wall_us_) * replay_speed_;
    replay_last_wall_us_ = now;
    const auto deadline = static_cast<std::int64_t>(
        recorded_events_.front().timestamp_us + replay_position_us_);

    const std::size_t begin = replay_cursor_;
    const std::size_t maximum = output.capacity() == 0
                                    ? recorded_events_.size()
                                    : output.capacity();
    while (replay_cursor_ < recorded_events_.size() &&
           replay_cursor_ - begin < maximum &&
           recorded_events_[replay_cursor_].timestamp_us <= deadline) {
        ++replay_cursor_;
    }
    output.assign(recorded_events_.begin() + begin,
                  recorded_events_.begin() + replay_cursor_);
    if (replay_cursor_ == recorded_events_.size()) mode_ = Mode::Stopped;
    return output.size();
}

std::size_t EventSource::read_image(
    std::vector<EventCamera::Event>& output) {
    output = image_events_;
    const std::int64_t timestamp = static_cast<std::int64_t>(timing::us());
    for (EventCamera::Event& event : output) event.timestamp_us = timestamp;
    return output.size();
}

void EventSource::set_error(std::string message) {
    last_error_ = std::move(message);
}
