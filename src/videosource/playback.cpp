#include "videosource/playback.hpp"
#include "utils/timing.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <vector>

#if defined(HAVE_OPENEB)
#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/stream/camera.h>
#endif

class Playback::Impl {
public:
#if defined(HAVE_OPENEB)
    std::unique_ptr<Metavision::Camera> camera;
#endif
    std::deque<Event> events;
    static constexpr std::size_t max_buffered_events = 1'000'000;
    std::int64_t timestamp_offset = std::numeric_limits<std::int64_t>::min();
    std::int64_t decoded_until = std::numeric_limits<std::int64_t>::min();
    bool stopped = false;
    int roi_x = 0;
    int roi_y = 0;
    int width = 0;
    int height = 0;
    mutable std::mutex mutex;
    std::condition_variable events_ready;
    std::condition_variable buffer_space;
};

Playback::Playback(const std::string& path, bool realtime)
    : Playback(path, 0, 0, 0, 0, realtime) {}

Playback::Playback(const std::string& path, int x, int y, int width, int height,
                   bool realtime)
    : impl_(std::make_unique<Impl>()) {
#if defined(HAVE_OPENEB)
    Metavision::FileConfigHints hints;
    hints.real_time_playback(realtime);
    impl_->camera = std::make_unique<Metavision::Camera>(Metavision::Camera::from_file(path, hints));
    impl_->roi_x = x;
    impl_->roi_y = y;
    impl_->width = width == 0 ? impl_->camera->geometry().get_width() : width;
    impl_->height = height == 0 ? impl_->camera->geometry().get_height() : height;
    impl_->camera->cd().add_callback([this](const Metavision::EventCD* begin, const Metavision::EventCD* end) {
        std::unique_lock<std::mutex> lock(impl_->mutex);
        if (impl_->timestamp_offset == std::numeric_limits<std::int64_t>::min())
            impl_->timestamp_offset = timing::us() - begin->t;
        for (auto event = begin; event != end; ++event) {
            if (event->x >= impl_->roi_x && event->x < impl_->roi_x + impl_->width &&
                event->y >= impl_->roi_y && event->y < impl_->roi_y + impl_->height) {
                if (impl_->events.size() >= Impl::max_buffered_events) {
                    impl_->events_ready.notify_all();
                    impl_->buffer_space.wait(lock, [this] {
                        return impl_->events.size() < Impl::max_buffered_events ||
                               impl_->stopped;
                    });
                    if (impl_->stopped) return;
                }

                impl_->events.push_back({event->t + impl_->timestamp_offset,
                                         event->x, event->y,
                                         event->p != 0, {}});
                impl_->events.back().x -= impl_->roi_x;
                impl_->events.back().y -= impl_->roi_y;
            }
            impl_->decoded_until = event->t + impl_->timestamp_offset;
        }
        impl_->events_ready.notify_all();
    });
    impl_->camera->add_status_change_callback([this](const Metavision::CameraStatus& status) {
        if (status != Metavision::CameraStatus::STOPPED) return;
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->stopped = true;
        }
        impl_->events_ready.notify_all();
        impl_->buffer_space.notify_all();
    });
    if (!impl_->camera->start()) throw std::runtime_error("Could not start RAW playback");
#else
    throw std::runtime_error("OpenEB support is not enabled in this build");
#endif
}

Playback::~Playback() {
    stop();
}

std::size_t Playback::read_events(std::vector<Event>& output, std::int64_t until_timestamp_us) {
    std::unique_lock<std::mutex> lock(impl_->mutex);
    output.clear();

    while (true) {
        const auto end = std::upper_bound(impl_->events.begin(),
                                          impl_->events.end(),
                                          until_timestamp_us,
                                          [](std::int64_t timestamp,
                                             const Event& event) {
                                              return timestamp < event.timestamp_us;
                                          });
        if (end != impl_->events.begin()) {
            output.insert(output.end(), impl_->events.begin(), end);
            impl_->events.erase(impl_->events.begin(), end);
            impl_->buffer_space.notify_all();
        }

        if (impl_->decoded_until >= until_timestamp_us || impl_->stopped) break;

        impl_->events_ready.wait(lock, [this, until_timestamp_us] {
            return !impl_->events.empty() ||
                   impl_->decoded_until >= until_timestamp_us ||
                   impl_->stopped;
        });
    }

    return output.size();
}

int Playback::width() const {
    return impl_->width;
}

int Playback::height() const {
    return impl_->height;
}

bool Playback::is_running() const {
#if defined(HAVE_OPENEB)
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return !impl_->stopped || !impl_->events.empty();
#else
    return false;
#endif
}

void Playback::stop() {
#if defined(HAVE_OPENEB)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->stopped = true;
    }
    impl_->events_ready.notify_all();
    impl_->buffer_space.notify_all();
    if (impl_->camera && impl_->camera->is_running()) impl_->camera->stop();
#endif
}
