#include "event_camera.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <mutex>
#include <utility>

#include "constants.hpp"

#if defined(HAVE_OPENEB)
#include <metavision/hal/facilities/i_ll_biases.h>
#include <metavision/hal/facilities/i_roi.h>
#include <metavision/hal/facilities/i_trigger_in.h>
#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/base/events/event_ext_trigger.h>
#include <metavision/sdk/stream/camera.h>
#endif

namespace {
std::size_t power_of_two_at_least(std::size_t requested) {
    constexpr std::size_t minimum = 1024;
    requested = std::max(requested, minimum);
    std::size_t result = 1;
    while (result < requested &&
           result <= std::numeric_limits<std::size_t>::max() / 2U) {
        result <<= 1U;
    }
    return result;
}
} // namespace

class EventCamera::Impl {
public:
    explicit Impl(std::size_t requested_capacity)
        : capacity_(power_of_two_at_least(requested_capacity)),
          mask_(capacity_ - 1U) {}

    void allocate_ring() {
        if (ring_.empty()) ring_.resize(capacity_);
        if (trigger_ring_.empty()) trigger_ring_.resize(trigger_capacity_);
        read_position_.store(0, std::memory_order_relaxed);
        write_position_.store(0, std::memory_order_relaxed);
        dropped_.store(0, std::memory_order_relaxed);
        trigger_read_position_.store(0, std::memory_order_relaxed);
        trigger_write_position_.store(0, std::memory_order_relaxed);
        dropped_triggers_.store(0, std::memory_order_relaxed);
        trigger_input_enabled_.store(false, std::memory_order_relaxed);
    }

    std::size_t read(Event* destination, std::size_t maximum) noexcept {
        if (!destination || maximum == 0 || ring_.empty()) return 0;
        const std::uint64_t read = read_position_.load(std::memory_order_relaxed);
        const std::uint64_t write = write_position_.load(std::memory_order_acquire);
        const auto available = static_cast<std::size_t>(write - read);
        const std::size_t count = std::min(maximum, available);
        const std::size_t index = static_cast<std::size_t>(read) & mask_;
        const std::size_t first = std::min(count, capacity_ - index);

        const bool make_local =
            local_roi_coordinates_.load(std::memory_order_relaxed) &&
            roi_enabled_.load(std::memory_order_relaxed);
        std::size_t produced = count;
        if (!make_local) {
            std::memcpy(destination, ring_.data() + index,
                        first * sizeof(Event));
            std::memcpy(destination + first, ring_.data(),
                        (count - first) * sizeof(Event));
        } else {
            const int offset_x = roi_x_.load(std::memory_order_relaxed);
            const int offset_y = roi_y_.load(std::memory_order_relaxed);
            const int roi_width = roi_width_.load(std::memory_order_relaxed);
            const int roi_height = roi_height_.load(std::memory_order_relaxed);
            produced = 0;
            const auto copy_local = [&](const Event* source,
                                        std::size_t source_count) {
                for (std::size_t i = 0; i < source_count; ++i) {
                    const int local_x = static_cast<int>(source[i].x) - offset_x;
                    const int local_y = static_cast<int>(source[i].y) - offset_y;
                    // Events already in flight during a hardware ROI change can
                    // still fall outside the new window. Discard them here.
                    if (local_x < 0 || local_y < 0 || local_x >= roi_width ||
                        local_y >= roi_height)
                        continue;
                    destination[produced] = source[i];
                    destination[produced].x =
                        static_cast<std::uint16_t>(local_x);
                    destination[produced].y =
                        static_cast<std::uint16_t>(local_y);
                    ++produced;
                }
            };
            copy_local(ring_.data() + index, first);
            copy_local(ring_.data(), count - first);
        }
        read_position_.store(read + count, std::memory_order_release);
        return produced;
    }

    std::size_t pending() const noexcept {
        const std::uint64_t read = read_position_.load(std::memory_order_acquire);
        const std::uint64_t write = write_position_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(write - read);
    }

    void clear() noexcept {
        const std::uint64_t write = write_position_.load(std::memory_order_acquire);
        read_position_.store(write, std::memory_order_release);
    }

    std::size_t read_triggers(TriggerEvent* destination, std::size_t maximum) noexcept {
        if (!destination || maximum == 0 || trigger_ring_.empty()) return 0;
        const std::uint64_t read = trigger_read_position_.load(std::memory_order_relaxed);
        const std::uint64_t write = trigger_write_position_.load(std::memory_order_acquire);
        const std::size_t count = std::min(maximum, static_cast<std::size_t>(write - read));
        const std::size_t index = static_cast<std::size_t>(read) & trigger_mask_;
        const std::size_t first = std::min(count, trigger_capacity_ - index);
        std::memcpy(destination, trigger_ring_.data() + index, first * sizeof(TriggerEvent));
        std::memcpy(destination + first, trigger_ring_.data(), (count - first) * sizeof(TriggerEvent));
        trigger_read_position_.store(read + count, std::memory_order_release);
        return count;
    }

    std::size_t pending_triggers() const noexcept {
        const std::uint64_t read = trigger_read_position_.load(std::memory_order_acquire);
        const std::uint64_t write = trigger_write_position_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(write - read);
    }

    void clear_triggers() noexcept {
        const std::uint64_t write = trigger_write_position_.load(std::memory_order_acquire);
        trigger_read_position_.store(write, std::memory_order_release);
    }

    void set_error(std::string message) const {
        std::lock_guard<std::mutex> lock(error_mutex_);
        error_ = std::move(message);
    }

    void clear_error() const {
        std::lock_guard<std::mutex> lock(error_mutex_);
        error_.clear();
    }

    std::string error() const {
        std::lock_guard<std::mutex> lock(error_mutex_);
        return error_;
    }

#if defined(HAVE_OPENEB)
    void push(const Metavision::EventCD* begin,
              const Metavision::EventCD* end) noexcept {
        if (!begin || begin == end || ring_.empty()) return;

        const std::size_t incoming = static_cast<std::size_t>(end - begin);
        const std::uint64_t write =
            write_position_.load(std::memory_order_relaxed);
        const std::uint64_t read = read_position_.load(std::memory_order_acquire);
        const std::size_t free_space = capacity_ -
                                       static_cast<std::size_t>(write - read);
        const std::size_t accepted = std::min(incoming, free_space);
        std::size_t index = static_cast<std::size_t>(write) & mask_;

        // Two linear passes at most; no modulo operation in the hot loop.
        const std::size_t first = std::min(accepted, capacity_ - index);
        for (std::size_t i = 0; i < first; ++i) {
            const auto& source = begin[i];
            ring_[index + i] = {static_cast<std::int64_t>(source.t), source.x,
                                source.y,
                                static_cast<std::uint8_t>(source.p), {0, 0, 0}};
        }
        for (std::size_t i = first; i < accepted; ++i) {
            const auto& source = begin[i];
            ring_[i - first] = {static_cast<std::int64_t>(source.t), source.x,
                                source.y,
                                static_cast<std::uint8_t>(source.p), {0, 0, 0}};
        }
        write_position_.store(write + accepted, std::memory_order_release);
        if (accepted != incoming) {
            dropped_.fetch_add(incoming - accepted, std::memory_order_relaxed);
        }
    }

    void push_triggers(const Metavision::EventExtTrigger* begin, const Metavision::EventExtTrigger* end, std::atomic<std::int64_t>& last_rising_timestamp) noexcept {
        if (!begin || begin == end || trigger_ring_.empty()) return;
        const std::size_t incoming = static_cast<std::size_t>(end - begin);
        const std::uint64_t write = trigger_write_position_.load(std::memory_order_relaxed);
        const std::uint64_t read = trigger_read_position_.load(std::memory_order_acquire);
        const std::size_t free_space = trigger_capacity_ - static_cast<std::size_t>(write - read);
        const std::size_t accepted = std::min(incoming, free_space);
        for (std::size_t i = 0; i < incoming; ++i) {
            if (begin[i].p != 0) {
                last_rising_timestamp.store(static_cast<std::int64_t>(begin[i].t), std::memory_order_release);
                std::cout << "trigger!" << std::endl;
            }
            if (i < accepted) {
                trigger_ring_[(static_cast<std::size_t>(write) + i) & trigger_mask_] =
                    {static_cast<std::int64_t>(begin[i].t), static_cast<std::int16_t>(begin[i].id),
                     static_cast<std::uint8_t>(begin[i].p), {0, 0, 0, 0, 0}};
            }
        }
        trigger_write_position_.store(write + accepted, std::memory_order_release);
        if (accepted != incoming) dropped_triggers_.fetch_add(incoming - accepted, std::memory_order_relaxed);
    }

    bool enable_main_trigger_input() {
        auto* trigger_input = camera_->get_device().get_facility<Metavision::I_TriggerIn>();
        if (!trigger_input) {
            trigger_input_enabled_.store(false, std::memory_order_release);
            return false;
        }
        const auto channels = trigger_input->get_available_channels();
        if (channels.find(Metavision::I_TriggerIn::Channel::Main) == channels.end()) {
            trigger_input_enabled_.store(false, std::memory_order_release);
            return false;
        }
        const bool enabled = trigger_input->is_enabled(Metavision::I_TriggerIn::Channel::Main) || trigger_input->enable(Metavision::I_TriggerIn::Channel::Main);
        trigger_input_enabled_.store(enabled, std::memory_order_release);
        return enabled;
    }

    void configure_streams(EventCamera* owner) {
        camera_->cd().add_callback([this](const Metavision::EventCD* begin, const Metavision::EventCD* end) {
            push(begin, end);
        });
        try {
            camera_->ext_trigger().add_callback([this, owner](const Metavision::EventExtTrigger* begin, const Metavision::EventExtTrigger* end) {
                push_triggers(begin, end, owner->last_rising_trigger_timestamp_us);
            });
            enable_main_trigger_input();
        } catch (...) {
            trigger_input_enabled_.store(false, std::memory_order_release);
        }
    }

    std::unique_ptr<Metavision::Camera> camera_;
#endif

    const std::size_t capacity_;
    const std::size_t mask_;
    std::vector<Event> ring_;
    alignas(64) std::atomic<std::uint64_t> read_position_{0};
    alignas(64) std::atomic<std::uint64_t> write_position_{0};
    std::atomic<std::uint64_t> dropped_{0};
    static constexpr std::size_t trigger_capacity_ = 1U << 12U;
    static constexpr std::size_t trigger_mask_ = trigger_capacity_ - 1U;
    std::vector<TriggerEvent> trigger_ring_;
    alignas(64) std::atomic<std::uint64_t> trigger_read_position_{0};
    alignas(64) std::atomic<std::uint64_t> trigger_write_position_{0};
    std::atomic<std::uint64_t> dropped_triggers_{0};
    std::atomic<bool> trigger_input_enabled_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> local_roi_coordinates_{false};
    std::atomic<bool> roi_enabled_{false};
    std::atomic<int> roi_x_{0};
    std::atomic<int> roi_y_{0};
    std::atomic<int> roi_width_{0};
    std::atomic<int> roi_height_{0};
    int width_ = 0;
    int height_ = 0;
    mutable std::mutex control_mutex_;
    mutable std::mutex error_mutex_;
    mutable std::string error_;
};

EventCamera::EventCamera(std::size_t event_capacity): impl_(std::make_unique<Impl>(event_capacity)) {
    open_first();
    load_settings(config::render::camera_settings_path);
    set_roi_coordinates_local(true);
    start();
    
}

EventCamera::~EventCamera() { close(); }

bool EventCamera::open_first() {
#if defined(HAVE_OPENEB)
    close();
    try {
        std::lock_guard<std::mutex> lock(impl_->control_mutex_);
        impl_->allocate_ring();
        impl_->camera_ = std::make_unique<Metavision::Camera>(
            Metavision::Camera::from_first_available());
        impl_->width_ = impl_->camera_->geometry().get_width();
        impl_->height_ = impl_->camera_->geometry().get_height();
        last_rising_trigger_timestamp_us.store(-1, std::memory_order_release);
        impl_->configure_streams(this);
        impl_->clear_error();
        return true;
    } catch (const std::exception& exception) {
#if defined(HAVE_OPENEB)
        std::lock_guard<std::mutex> lock(impl_->control_mutex_);
        impl_->camera_.reset();
#endif
        impl_->set_error(exception.what());
        return false;
    }
#else
    impl_->set_error("OpenEB support is not enabled in this build");
    return false;
#endif
}

bool EventCamera::open_usb(std::uint32_t device_index) {
#if defined(HAVE_OPENEB)
    close();
    try {
        std::lock_guard<std::mutex> lock(impl_->control_mutex_);
        impl_->allocate_ring();
        impl_->camera_ = std::make_unique<Metavision::Camera>(
            Metavision::Camera::from_source(Metavision::OnlineSourceType::USB,
                                            device_index));
        impl_->width_ = impl_->camera_->geometry().get_width();
        impl_->height_ = impl_->camera_->geometry().get_height();
        last_rising_trigger_timestamp_us.store(-1, std::memory_order_release);
        impl_->configure_streams(this);
        impl_->clear_error();
        return true;
    } catch (const std::exception& exception) {
        std::lock_guard<std::mutex> lock(impl_->control_mutex_);
        impl_->camera_.reset();
        impl_->set_error(exception.what());
        return false;
    }
#else
    static_cast<void>(device_index);
    impl_->set_error("OpenEB support is not enabled in this build");
    return false;
#endif
}

bool EventCamera::open_serial(const std::string& serial) {
#if defined(HAVE_OPENEB)
    close();
    try {
        std::lock_guard<std::mutex> lock(impl_->control_mutex_);
        impl_->allocate_ring();
        impl_->camera_ = std::make_unique<Metavision::Camera>(
            Metavision::Camera::from_serial(serial));
        impl_->width_ = impl_->camera_->geometry().get_width();
        impl_->height_ = impl_->camera_->geometry().get_height();
        last_rising_trigger_timestamp_us.store(-1, std::memory_order_release);
        impl_->configure_streams(this);
        impl_->clear_error();
        return true;
    } catch (const std::exception& exception) {
        std::lock_guard<std::mutex> lock(impl_->control_mutex_);
        impl_->camera_.reset();
        impl_->set_error(exception.what());
        return false;
    }
#else
    static_cast<void>(serial);
    impl_->set_error("OpenEB support is not enabled in this build");
    return false;
#endif
}

void EventCamera::close() {
    stop();
#if defined(HAVE_OPENEB)
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    impl_->camera_.reset();
#endif
    impl_->width_ = 0;
    impl_->height_ = 0;
    impl_->roi_enabled_.store(false, std::memory_order_relaxed);
    impl_->roi_x_.store(0, std::memory_order_relaxed);
    impl_->roi_y_.store(0, std::memory_order_relaxed);
    impl_->roi_width_.store(0, std::memory_order_relaxed);
    impl_->roi_height_.store(0, std::memory_order_relaxed);
    impl_->clear();
    impl_->clear_triggers();
    impl_->trigger_input_enabled_.store(false, std::memory_order_release);
    last_rising_trigger_timestamp_us.store(-1, std::memory_order_release);
}

bool EventCamera::start() {
#if defined(HAVE_OPENEB)
    try {
        std::lock_guard<std::mutex> lock(impl_->control_mutex_);
        if (!impl_->camera_) {
            impl_->set_error("No event camera is open");
            return false;
        }
        impl_->enable_main_trigger_input();
        if (!impl_->camera_->start()) {
            impl_->set_error("OpenEB could not start the camera stream");
            return false;
        }
        impl_->running_.store(true, std::memory_order_release);
        impl_->clear_error();
        return true;
    } catch (const std::exception& exception) {
        impl_->set_error(exception.what());
        return false;
    }
#else
    impl_->set_error("OpenEB support is not enabled in this build");
    return false;
#endif
}

void EventCamera::stop() {
#if defined(HAVE_OPENEB)
    try {
        std::lock_guard<std::mutex> lock(impl_->control_mutex_);
        if (impl_->camera_ && impl_->running_.load(std::memory_order_acquire)) {
            impl_->camera_->stop();
        }
    } catch (const std::exception& exception) {
        impl_->set_error(exception.what());
    }
#endif
    impl_->running_.store(false, std::memory_order_release);
}

bool EventCamera::is_open() const {
#if defined(HAVE_OPENEB)
    std::lock_guard<std::mutex> lock(impl_->control_mutex_);
    return static_cast<bool>(impl_->camera_);
#else
    return false;
#endif
}

bool EventCamera::is_running() const {
    return impl_->running_.load(std::memory_order_acquire);
}

int EventCamera::width() const { return impl_->width_; }
int EventCamera::height() const { return impl_->height_; }
int EventCamera::roi_width() const { return impl_->roi_width_; }
int EventCamera::roi_height() const { return impl_->roi_height_; }

std::size_t EventCamera::read_events(Event* destination,
                                     std::size_t maximum) noexcept {
    return impl_->read(destination, maximum);
}

std::size_t EventCamera::read_events(std::vector<Event>& output,
                                     std::size_t maximum) {
    const std::size_t available = impl_->pending();
    // Once the caller has reserved storage, the default read never grows it.
    // This makes repeated reads allocation-free without requiring a raw buffer.
    const std::size_t implicit_limit = output.capacity() == 0 ? available : output.capacity();
    const std::size_t limit = maximum == 0 ? implicit_limit : maximum;
    const std::size_t count = std::min(limit, available);
    output.resize(count);
    const std::size_t read = impl_->read(output.data(), count);
    output.resize(read);
    return read;
}

std::size_t EventCamera::pending_events() const noexcept {
    return impl_->pending();
}

std::size_t EventCamera::event_capacity() const noexcept {
    return impl_->capacity_;
}

std::uint64_t EventCamera::dropped_events() const noexcept {
    return impl_->dropped_.load(std::memory_order_relaxed);
}

void EventCamera::clear_events() noexcept { impl_->clear(); }

std::size_t EventCamera::read_trigger_events(TriggerEvent* destination, std::size_t maximum) noexcept {
    return impl_->read_triggers(destination, maximum);
}

std::size_t EventCamera::read_trigger_events(std::vector<TriggerEvent>& output, std::size_t maximum) {
    const std::size_t available = impl_->pending_triggers();
    const std::size_t implicit_limit = output.capacity() == 0 ? available : output.capacity();
    const std::size_t limit = maximum == 0 ? implicit_limit : maximum;
    const std::size_t count = std::min(limit, available);
    output.resize(count);
    const std::size_t read = impl_->read_triggers(output.data(), count);
    output.resize(read);
    return read;
}

std::size_t EventCamera::pending_trigger_events() const noexcept { return impl_->pending_triggers(); }

std::uint64_t EventCamera::dropped_trigger_events() const noexcept {
    return impl_->dropped_triggers_.load(std::memory_order_relaxed);
}

void EventCamera::clear_trigger_events() noexcept { impl_->clear_triggers(); }

bool EventCamera::trigger_input_enabled() const noexcept {
    return impl_->trigger_input_enabled_.load(std::memory_order_acquire);
}

bool EventCamera::set_bias(const std::string& name, int value) {
#if defined(HAVE_OPENEB)
    try {
        std::lock_guard<std::mutex> lock(impl_->control_mutex_);
        if (!impl_->camera_) {
            impl_->set_error("No event camera is open");
            return false;
        }
        const bool success =
            impl_->camera_->get_facility<Metavision::I_LL_Biases>().set(name, value);
        if (!success) impl_->set_error("OpenEB rejected bias " + name);
        return success;
    } catch (const std::exception& exception) {
        impl_->set_error(exception.what());
        return false;
    }
#else
    static_cast<void>(name);
    static_cast<void>(value);
    impl_->set_error("OpenEB support is not enabled in this build");
    return false;
#endif
}

std::optional<int> EventCamera::bias(const std::string& name) const {
#if defined(HAVE_OPENEB)
    try {
        std::lock_guard<std::mutex> lock(impl_->control_mutex_);
        if (!impl_->camera_) {
            impl_->set_error("No event camera is open");
            return std::nullopt;
        }
        return impl_->camera_->get_facility<Metavision::I_LL_Biases>().get(name);
    } catch (const std::exception& exception) {
        impl_->set_error(exception.what());
        return std::nullopt;
    }
#else
    static_cast<void>(name);
    impl_->set_error("OpenEB support is not enabled in this build");
    return std::nullopt;
#endif
}

std::map<std::string, int> EventCamera::biases() const {
#if defined(HAVE_OPENEB)
    try {
        std::lock_guard<std::mutex> lock(impl_->control_mutex_);
        if (!impl_->camera_) {
            impl_->set_error("No event camera is open");
            return {};
        }
        return impl_->camera_->get_facility<Metavision::I_LL_Biases>()
            .get_all_biases();
    } catch (const std::exception& exception) {
        impl_->set_error(exception.what());
        return {};
    }
#else
    impl_->set_error("OpenEB support is not enabled in this build");
    return {};
#endif
}

std::vector<EventCamera::BiasSetting> EventCamera::bias_settings() const {
#if defined(HAVE_OPENEB)
    try {
        std::lock_guard<std::mutex> lock(impl_->control_mutex_);
        if (!impl_->camera_) {
            impl_->set_error("No event camera is open");
            return {};
        }
        auto& facility =
            impl_->camera_->get_facility<Metavision::I_LL_Biases>();
        std::vector<BiasSetting> result;
        const auto values = facility.get_all_biases();
        result.reserve(values.size());
        for (const auto& [name, value] : values) {
            Metavision::LL_Bias_Info information;
            if (!facility.get_bias_info(name, information)) continue;
            const auto range = information.get_bias_range();
            result.push_back(
                {name, value, range.first, range.second,
                 information.is_modifiable()});
        }
        return result;
    } catch (const std::exception& exception) {
        impl_->set_error(exception.what());
        return {};
    }
#else
    impl_->set_error("OpenEB support is not enabled in this build");
    return {};
#endif
}

bool EventCamera::load_settings(const std::string& path) {
#if defined(HAVE_OPENEB)
    try {
        std::lock_guard<std::mutex> lock(impl_->control_mutex_);
        if (!impl_->camera_) {
            impl_->set_error("No event camera is open");
            return false;
        }
        if (!impl_->camera_->load(path)) {
            impl_->set_error("OpenEB could not load camera settings from " + path);
            return false;
        }

        auto& roi = impl_->camera_->get_facility<Metavision::I_ROI>();
        const auto windows = roi.get_windows();
        if (!windows.empty()) {
            const auto& window = windows.front();
            impl_->roi_x_.store(window.x, std::memory_order_relaxed);
            impl_->roi_y_.store(window.y, std::memory_order_relaxed);
            impl_->roi_width_.store(window.width, std::memory_order_relaxed);
            impl_->roi_height_.store(window.height, std::memory_order_relaxed);
        } else {
            impl_->roi_x_.store(0, std::memory_order_relaxed);
            impl_->roi_y_.store(0, std::memory_order_relaxed);
            impl_->roi_width_.store(impl_->width_, std::memory_order_relaxed);
            impl_->roi_height_.store(impl_->height_, std::memory_order_relaxed);
        }
        impl_->roi_enabled_.store(roi.is_enabled(), std::memory_order_release);
        impl_->clear();
        impl_->clear_error();
        return true;
    } catch (const std::exception& exception) {
        impl_->set_error(exception.what());
        return false;
    }
#else
    static_cast<void>(path);
    impl_->set_error("OpenEB support is not enabled in this build");
    return false;
#endif
}

bool EventCamera::save_settings(const std::string& path) const {
#if defined(HAVE_OPENEB)
    try {
        std::lock_guard<std::mutex> lock(impl_->control_mutex_);
        if (!impl_->camera_) {
            impl_->set_error("No event camera is open");
            return false;
        }
        const std::filesystem::path settings_path(path);
        if (settings_path.has_parent_path())
            std::filesystem::create_directories(settings_path.parent_path());
        if (!impl_->camera_->save(settings_path)) {
            impl_->set_error("OpenEB could not save camera settings to " + path);
            return false;
        }
        impl_->clear_error();
        return true;
    } catch (const std::exception& exception) {
        impl_->set_error(exception.what());
        return false;
    }
#else
    static_cast<void>(path);
    impl_->set_error("OpenEB support is not enabled in this build");
    return false;
#endif
}

bool EventCamera::set_roi(int x, int y, int width, int height) {
#if defined(HAVE_OPENEB)
    if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
        x >= impl_->width_ || y >= impl_->height_ ||
        width > impl_->width_ - x || height > impl_->height_ - y) {
        impl_->set_error("ROI lies outside the camera sensor");
        return false;
    }
    try {
        std::lock_guard<std::mutex> lock(impl_->control_mutex_);
        if (!impl_->camera_) {
            impl_->set_error("No event camera is open");
            return false;
        }
        auto& roi = impl_->camera_->get_facility<Metavision::I_ROI>();
        const bool success = roi.set_mode(Metavision::I_ROI::Mode::ROI) &&
                             roi.set_window({x, y, width, height}) &&
                             roi.enable(true);
        if (success) {
            impl_->roi_x_.store(x, std::memory_order_relaxed);
            impl_->roi_y_.store(y, std::memory_order_relaxed);
            impl_->roi_width_.store(width, std::memory_order_relaxed);
            impl_->roi_height_.store(height, std::memory_order_relaxed);
            impl_->roi_enabled_.store(true, std::memory_order_release);
            impl_->clear();
        } else {
            impl_->set_error("OpenEB rejected the ROI configuration");
        }
        return success;
    } catch (const std::exception& exception) {
        impl_->set_error(exception.what());
        return false;
    }
#else
    static_cast<void>(x);
    static_cast<void>(y);
    static_cast<void>(width);
    static_cast<void>(height);
    impl_->set_error("OpenEB support is not enabled in this build");
    return false;
#endif
}

bool EventCamera::enable_roi(bool enabled) {
#if defined(HAVE_OPENEB)
    try {
        std::lock_guard<std::mutex> lock(impl_->control_mutex_);
        if (!impl_->camera_) {
            impl_->set_error("No event camera is open");
            return false;
        }
        const bool success =
            impl_->camera_->get_facility<Metavision::I_ROI>().enable(enabled);
        if (success) {
            impl_->roi_enabled_.store(enabled, std::memory_order_release);
            impl_->clear();
        } else {
            impl_->set_error("OpenEB could not change the ROI state");
        }
        return success;
    } catch (const std::exception& exception) {
        impl_->set_error(exception.what());
        return false;
    }
#else
    static_cast<void>(enabled);
    impl_->set_error("OpenEB support is not enabled in this build");
    return false;
#endif
}

std::optional<EventCamera::RoiSetting> EventCamera::roi_setting() const noexcept {
    const int width = impl_->roi_width_.load(std::memory_order_acquire);
    const int height = impl_->roi_height_.load(std::memory_order_acquire);
    if (width <= 0 || height <= 0) return std::nullopt;
    return RoiSetting{impl_->roi_x_.load(std::memory_order_relaxed),
                      impl_->roi_y_.load(std::memory_order_relaxed), width,
                      height,
                      impl_->roi_enabled_.load(std::memory_order_acquire)};
}

void EventCamera::set_roi_coordinates_local(bool enabled) noexcept {
    impl_->local_roi_coordinates_.store(enabled, std::memory_order_release);
    impl_->clear();
}

bool EventCamera::roi_coordinates_local() const noexcept {
    return impl_->local_roi_coordinates_.load(std::memory_order_acquire);
}

std::string EventCamera::last_error() const { return impl_->error(); }

bool EventCamera::openeb_available() noexcept {
#if defined(HAVE_OPENEB)
    return true;
#else
    return false;
#endif
}
