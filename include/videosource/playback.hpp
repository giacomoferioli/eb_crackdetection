#pragma once

#include "videosource/event_source.hpp"

#include <memory>
#include <string>

// Plays a RAW EVT3 recording through the same interface as the live camera.
class Playback final : public EventSource {
public:
    explicit Playback(const std::string& path, bool realtime = false);
    Playback(const std::string& path, int x, int y, int width, int height,
             bool realtime = false);
    ~Playback();

    Playback(const Playback&) = delete;
    Playback& operator=(const Playback&) = delete;

    std::size_t read_events(std::vector<Event>& output,
                            std::int64_t until_timestamp_us) override;
    int width() const override;
    int height() const override;
    bool is_running() const override;
    void stop() override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
