#pragma once

#include "videosource/event_source.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>

// Sequential reader for EBLITE recordings. Decoding is synchronous and uses a
// large input buffer, so no decoder thread or intermediate event queue is needed.
class EblitePlayback final : public EventSource {
public:
    explicit EblitePlayback(const std::filesystem::path& path);
    ~EblitePlayback();

    EblitePlayback(const EblitePlayback&) = delete;
    EblitePlayback& operator=(const EblitePlayback&) = delete;

    std::size_t read_events(std::vector<Event>& output,
                            std::int64_t until_timestamp_us) override;
    int width() const override;
    int height() const override;
    bool is_running() const override;
    void stop() override;

    std::uint64_t event_count() const;
    std::uint64_t recording_duration_us() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
