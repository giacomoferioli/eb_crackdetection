#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/stream/camera.h>
#include <metavision/sdk/stream/camera_exception.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint64_t bin_width_us = 10;
constexpr std::uint64_t dense_limit_us = 10'000'000;
constexpr std::size_t dense_bin_count = dense_limit_us / bin_width_us;
constexpr std::size_t histogram_width = 60;
constexpr std::size_t max_histogram_rows = 100;

struct Bin {
    std::uint64_t index;
    std::uint64_t count;
};

class DeltaHistogram {
public:
    DeltaHistogram() : dense_bins_(dense_bin_count, 0) {}

    void add(const Metavision::EventCD *begin,
             const Metavision::EventCD *end) noexcept {
        if (!begin || begin == end || failed_)
            return;

        try {
            for (const Metavision::EventCD *event = begin; event != end; ++event) {
                const std::int64_t timestamp = event->t;
                ++events_;

                if (!have_previous_) {
                    previous_timestamp_ = timestamp;
                    have_previous_ = true;
                    continue;
                }

                if (timestamp < previous_timestamp_) {
                    ++timestamp_regressions_;
                    previous_timestamp_ = timestamp;
                    continue;
                }

                const std::uint64_t delta =
                    static_cast<std::uint64_t>(timestamp - previous_timestamp_);
                const std::uint64_t bin_index = delta / bin_width_us;
                if (bin_index < dense_bins_.size())
                    ++dense_bins_[static_cast<std::size_t>(bin_index)];
                else
                    ++sparse_bins_[bin_index];

                ++deltas_;
                previous_timestamp_ = timestamp;
            }
            published_events_.store(events_, std::memory_order_relaxed);
        } catch (...) {
            // Never let an exception escape through the SDK callback boundary.
            failed_ = true;
        }
    }

    std::uint64_t published_events() const noexcept {
        return published_events_.load(std::memory_order_relaxed);
    }

    std::uint64_t events() const noexcept { return events_; }
    std::uint64_t deltas() const noexcept { return deltas_; }
    std::uint64_t timestamp_regressions() const noexcept {
        return timestamp_regressions_;
    }
    bool failed() const noexcept { return failed_; }

    std::vector<Bin> nonempty_bins() const {
        std::vector<Bin> result;
        for (std::size_t i = 0; i < dense_bins_.size(); ++i) {
            if (dense_bins_[i] != 0)
                result.push_back({static_cast<std::uint64_t>(i), dense_bins_[i]});
        }
        for (const auto &[index, count] : sparse_bins_)
            result.push_back({index, count});
        return result;
    }

private:
    std::vector<std::uint64_t> dense_bins_;
    std::map<std::uint64_t, std::uint64_t> sparse_bins_;
    std::atomic<std::uint64_t> published_events_{0};
    std::uint64_t events_ = 0;
    std::uint64_t deltas_ = 0;
    std::uint64_t timestamp_regressions_ = 0;
    std::int64_t previous_timestamp_ = 0;
    bool have_previous_ = false;
    bool failed_ = false;
};

void write_csv(const std::filesystem::path &path,
               const std::vector<Bin> &bins,
               std::uint64_t delta_count) {
    std::ofstream output(path);
    if (!output)
        throw std::runtime_error("could not open output CSV: " + path.string());

    output << "bin_start_us,bin_end_us,event_count,percentage\n";
    output << std::setprecision(12);
    for (const Bin &bin : bins) {
        const std::uint64_t start = bin.index * bin_width_us;
        const std::uint64_t end = start + bin_width_us;
        const double percentage = delta_count == 0
                                      ? 0.0
                                      : 100.0 * static_cast<double>(bin.count) /
                                            static_cast<double>(delta_count);
        output << start << ',' << end << ',' << bin.count << ','
               << percentage << '\n';
    }

    if (!output)
        throw std::runtime_error("failed while writing output CSV: " + path.string());
}

void print_histogram(const std::vector<Bin> &bins, std::uint64_t delta_count) {
    if (bins.empty() || delta_count == 0) {
        std::cout << "No timestamp deltas were found.\n";
        return;
    }

    const auto maximum = std::max_element(
        bins.begin(), bins.end(),
        [](const Bin &left, const Bin &right) { return left.count < right.count; });
    const std::uint64_t maximum_count = maximum->count;
    const std::size_t shown = std::min(bins.size(), max_histogram_rows);

    std::cout << "\nTimestamp delta histogram (10 us buckets)\n";
    for (std::size_t i = 0; i < shown; ++i) {
        const Bin &bin = bins[i];
        const std::uint64_t start = bin.index * bin_width_us;
        const std::uint64_t end = start + bin_width_us;
        const double percentage =
            100.0 * static_cast<double>(bin.count) / static_cast<double>(delta_count);
        const std::size_t bar_length = static_cast<std::size_t>(
            (bin.count * histogram_width + maximum_count - 1) / maximum_count);

        std::cout << std::setw(10) << start << "-" << std::setw(10) << end
                  << " us  " << std::setw(11) << std::fixed << std::setprecision(7)
                  << percentage << "%  " << std::string(bar_length, '#') << '\n';
    }

    if (shown != bins.size())
        std::cout << "... " << (bins.size() - shown)
                  << " additional non-empty buckets are available in the CSV.\n";
}

void print_usage(const char *program) {
    std::cerr << "Usage: " << program << " INPUT.raw OUTPUT.csv\n";
}

} // namespace

int main(int argc, char **argv) {
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

    try {
        Metavision::FileConfigHints hints;
        hints.real_time_playback(false);
        Metavision::Camera camera = Metavision::Camera::from_file(input_path, hints);
        DeltaHistogram histogram;

        camera.cd().add_callback(
            [&histogram](const Metavision::EventCD *begin,
                         const Metavision::EventCD *end) {
                histogram.add(begin, end);
            });

        if (!camera.start())
            throw std::runtime_error("OpenEB could not start file decoding");

        std::uint64_t last_reported = 0;
        while (camera.is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            const std::uint64_t current = histogram.published_events();
            if (current != last_reported) {
                std::cerr << '\r' << current << " events analyzed" << std::flush;
                last_reported = current;
            }
        }
        camera.stop();
        std::cerr << '\r' << histogram.events() << " events analyzed\n";

        if (histogram.failed())
            throw std::runtime_error("histogram allocation failed during decoding");

        const std::vector<Bin> bins = histogram.nonempty_bins();
        write_csv(output_path, bins, histogram.deltas());

        std::cout << "Input events: " << histogram.events() << '\n'
                  << "Measured deltas: " << histogram.deltas() << '\n'
                  << "Timestamp regressions excluded: "
                  << histogram.timestamp_regressions() << '\n'
                  << "Non-empty buckets: " << bins.size() << '\n'
                  << "CSV: " << output_path << '\n';
        print_histogram(bins, histogram.deltas());
        return 0;
    } catch (const Metavision::CameraException &exception) {
        std::cerr << "OpenEB error: " << exception.what() << '\n';
    } catch (const std::exception &exception) {
        std::cerr << "Error: " << exception.what() << '\n';
    }
    return 1;
}
