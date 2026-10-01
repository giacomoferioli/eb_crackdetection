#include "videosource/playback.hpp"
#include "timing.hpp"

#include <algorithm>
#include <condition_variable>
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
    std::vector<Event> events;
    std::size_t read_position = 0;
    std::int64_t timestamp_offset = std::numeric_limits<std::int64_t>::min();
    std::int64_t decoded_until = std::numeric_limits<std::int64_t>::min();
    bool stopped = false;
    int width = 0;
    int height = 0;
    mutable std::mutex mutex;
    std::condition_variable events_ready;
};

Playback::Playback(const std::string& path) : impl_(std::make_unique<Impl>()) {
#if defined(HAVE_OPENEB)
    Metavision::FileConfigHints hints;
    hints.real_time_playback(false);
    impl_->camera = std::make_unique<Metavision::Camera>(Metavision::Camera::from_file(path, hints));
    impl_->width = impl_->camera->geometry().get_width();
    impl_->height = impl_->camera->geometry().get_height();
    impl_->camera->cd().add_callback([this](const Metavision::EventCD* begin, const Metavision::EventCD* end) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->timestamp_offset == std::numeric_limits<std::int64_t>::min())
            impl_->timestamp_offset = timing::us() - begin->t;
        for (auto event = begin; event != end; ++event)
            impl_->events.push_back({event->t + impl_->timestamp_offset, event->x,
                                     event->y, event->p != 0, {}});
        impl_->decoded_until = impl_->events.back().timestamp_us;
        impl_->events_ready.notify_all();
    });
    impl_->camera->add_status_change_callback([this](const Metavision::CameraStatus& status) {
        if (status != Metavision::CameraStatus::STOPPED) return;
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->stopped = true;
        }
        impl_->events_ready.notify_all();
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
    impl_->events_ready.wait(lock, [this, until_timestamp_us] {
        return impl_->decoded_until >= until_timestamp_us || impl_->stopped;
    });
    const auto end = std::upper_bound(impl_->events.begin() + impl_->read_position,
                                     impl_->events.end(), until_timestamp_us,
                                     [](std::int64_t timestamp, const Event& event) {
                                         return timestamp < event.timestamp_us;
                                     });
    const std::size_t count = end - (impl_->events.begin() + impl_->read_position);
    output.assign(impl_->events.begin() + impl_->read_position,
                  end);
    impl_->read_position += count;
    if (impl_->read_position == impl_->events.size()) {
        impl_->events.clear();
        impl_->read_position = 0;
    }
    return count;
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
    return !impl_->stopped || impl_->read_position != impl_->events.size();
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
    if (impl_->camera && impl_->camera->is_running()) impl_->camera->stop();
#endif
}
