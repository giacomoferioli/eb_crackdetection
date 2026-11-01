#include "eblite/writer.hpp"

#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/stream/camera.h>
#include <metavision/sdk/stream/camera_exception.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " INPUT.raw OUTPUT.ebl\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        print_usage(argv[0]);
        return 2;
    }

    const std::filesystem::path input_path(argv[1]);
    const std::filesystem::path output_path(argv[2]);
    if (!std::filesystem::is_regular_file(input_path)) {
        std::cerr << "Input is not a regular file: " << input_path << '\n';
        return 1;
    }
    if (output_path.extension() != ".ebl") {
        std::cerr << "EBL output path must end in .ebl: " << output_path << '\n';
        return 1;
    }

    try {
        Metavision::FileConfigHints hints;
        hints.real_time_playback(false);
        Metavision::Camera camera = Metavision::Camera::from_file(input_path, hints);
        const std::uint32_t width = camera.geometry().get_width();
        const std::uint32_t height = camera.geometry().get_height();
        eblite::Writer writer(output_path, width, height);

        std::atomic<std::uint64_t> published_events{0};
        std::atomic<bool> callback_failed{false};
        std::exception_ptr callback_error;
        std::vector<Event> converted_events;

        camera.cd().add_callback(
            [&](const Metavision::EventCD* begin,
                const Metavision::EventCD* end) {
                if (callback_failed.load(std::memory_order_relaxed) ||
                    !begin || begin == end)
                    return;
                try {
                    converted_events.clear();
                    converted_events.reserve(static_cast<std::size_t>(end - begin));
                    for (const Metavision::EventCD* event = begin; event != end; ++event) {
                        converted_events.push_back(
                            {event->t, event->x, event->y,
                             static_cast<std::uint8_t>(event->p != 0), {0, 0, 0}});
                    }
                    writer.write(converted_events);
                    published_events.store(writer.event_count(),
                                           std::memory_order_relaxed);
                } catch (...) {
                    callback_error = std::current_exception();
                    callback_failed.store(true, std::memory_order_release);
                }
            });

        if (!camera.start())
            throw std::runtime_error("OpenEB could not start RAW decoding");

        std::uint64_t last_reported = 0;
        while (camera.is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (callback_failed.load(std::memory_order_acquire)) {
                camera.stop();
                break;
            }
            const std::uint64_t current =
                published_events.load(std::memory_order_relaxed);
            if (current != last_reported) {
                std::cerr << '\r' << current << " events encoded" << std::flush;
                last_reported = current;
            }
        }
        camera.stop();

        if (callback_failed.load(std::memory_order_acquire))
            std::rethrow_exception(callback_error);
        writer.finalize();

        const std::uint64_t output_size = std::filesystem::file_size(output_path);
        const std::uint64_t input_size = std::filesystem::file_size(input_path);
        std::cerr << '\r' << writer.event_count() << " events encoded\n";
        std::cout << "Input: " << input_path << " (" << input_size << " bytes)\n"
                  << "Output: " << output_path << " (" << output_size << " bytes)\n"
                  << "Geometry: " << width << 'x' << height << '\n'
                  << "Duration: " << writer.recording_duration_us() << " us\n"
                  << "Events: " << writer.event_count() << '\n';
        if (input_size != 0) {
            const double percentage =
                100.0 * static_cast<double>(output_size) /
                static_cast<double>(input_size);
            std::cout << "Output/input size: " << percentage << "%\n";
        }
        return 0;
    } catch (const Metavision::CameraException& exception) {
        std::cerr << "OpenEB error: " << exception.what() << '\n';
    } catch (const std::exception& exception) {
        std::cerr << "Error: " << exception.what() << '\n';
    }
    return 1;
}
