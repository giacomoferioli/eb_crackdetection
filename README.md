# Event-camera specimen processing

This project reads the output of an event camera, assembles the events into
sliding pseudo-frames, and runs an ordered sequence of image processors. The
current processor, `Proc2b`, tracks a test specimen and estimates the horizontal
position of a crack.

An event camera does not produce conventional video frames. It reports changes
at individual pixels. Each `Event` contains a timestamp, pixel coordinates, and
a polarity. The application integrates those events over a configurable time
window to create an image that can be processed by frame-based algorithms.

## Architecture

```text
                    +-----------------+
RAW recording ----> |    Playback     |
                    +-----------------+
                              \
                               \
                                >-- EventSource --+
                               /                  |
Live camera ------> +-----------------+           v
                    |   EventCamera   |      +------------+
                    +-----------------+      | Processing |
                                             +------------+
                                                   |
                                      rolling pseudo-frame
                                                   |
                                                   v
                                      ordered FrameProcessors
                                     +------+------+------+
                                     |             |      |
                                   Proc1         Proc2  Proc2b
```

`EventSource` separates event acquisition from processing. `Processing` does
not need to know whether the events come from a camera or a recording. It owns
the pseudo-frame and invokes one `FrameProcessor` at a time.

## Event data and sources

### `Event`

Defined in `include/videosource/event_source.hpp`, an `Event` contains:

- `timestamp_us`: event time in microseconds;
- `x`, `y`: pixel coordinates;
- `polarity`: `0` for a negative change and `1` for a positive change.

When an event enters the pseudo-frame, it contributes `-1` or `+1` to its
pixel. Its contribution is removed when it becomes older than
`pseudoframe_length`.

### `TriggerEvent`

Represents an external hardware-trigger event. It records a timestamp, trigger
channel, and polarity. Trigger events are exposed by `EventCamera` but are not
currently used by the pseudo-frame processing loop.

### `EventSource`

`EventSource` is the abstract input interface. Implementations provide sensor
dimensions, running state, shutdown, and this operation:

```cpp
read_events(output, until_timestamp_us);
```

The operation returns events whose timestamp is no later than the requested
cutoff. The two implementations are `Playback` and `EventCamera`.

### `Playback`

`Playback` reads an OpenEB/Metavision RAW recording. OpenEB decodes the file on
its own thread and calls a callback with batches of events. `Playback` stores
accepted events in a mutex-protected deque and lets `Processing` drain them by
timestamp.

The playback buffer is limited to one million events. When it is full, the
decoder waits until processing creates space; recorded events are therefore not
dropped. The optional constructor ROI filters events and translates their
coordinates so the ROI starts at `(0, 0)`.

With `realtime: true`, OpenEB reproduces the original recording timing. With
`realtime: false`, processing advances through the recording as quickly as the
decoder and CPU allow.

### `EventCamera`

`EventCamera` wraps a live OpenEB camera. Its event callback writes into a
bounded single-producer/single-consumer ring buffer, while `Processing` reads
from the other side. The default capacity is `2^22` events, approximately
64 MiB.

Unlike playback, live acquisition cannot pause the sensor when processing is
slow. Events arriving when the ring is full are dropped and counted by
`dropped_events()`.

`EventCamera` also provides:

- opening the first, a numbered USB, or a serial-selected camera;
- camera start, stop, and close operations;
- hardware ROI configuration and optional ROI-local coordinates;
- camera bias reading and modification;
- loading and saving OpenEB camera settings;
- external-trigger buffering and the latest rising-trigger timestamp;
- error reporting through `last_error()`.

The constructor opens the first camera, loads the configured OpenEB camera
settings, enables local ROI coordinates, and starts the camera. EVT3 recording
is started and stopped separately by the live console and is flushed before
the camera stops.

## Pseudo-frame orchestration

### `Config`

`Config` contains the timing, event-rate, and saving settings loaded from
`config/config.cfg`:

| Field | Meaning |
|---|---|
| `realtime` | Reproduce and enforce real event timing when true. |
| `camera_config_path` | OpenEB camera-settings JSON loaded for live capture. |
| `raw_save_path` | `.raw` file receiving the live camera's EVT3 stream. |
| `save_interval` | Seconds between saved images. |
| `max_event_rate` | Expected maximum events per second, used for buffer sizing. |
| `pseudoframe_length` | Duration of the sliding event window in seconds. |
| `processing_interval` | Time between consecutive pseudo-frames. |
| `processing_start` | First time at which frame processors may run. |
| `processing_end` | Exclusive end of the frame-processing window. |
| `saving_start` | First time at which images may be saved. |
| `saving_end` | Exclusive end of the image-saving window. |

Times are relative to the first event in the stream.

The configuration format is one `key = value` pair per line. Empty lines and
lines whose first non-whitespace character is `#` are ignored:

```text
# Processing window, in seconds
processing_start = 150.0
processing_end = 1400.0
```

Comments occupy a complete line; inline comments after a value are not
supported. Setting order is unrestricted. Unknown settings, duplicate settings,
missing settings, and malformed values produce an error that identifies the
configuration line.

### `Processing`

`Processing` is the application runner. It holds a reference to the externally
owned `EventSource` and owns:

- the FIFO of events currently contributing to the pseudo-frame;
- the signed 16-bit pseudo-frame pixels;
- the reusable input-event buffer;
- the raw `ImageSaver`;
- the ordered list of frame processors and the active processor index.

For every frame tick it performs these steps:

1. Compute the exact frame timestamp.
2. Read source events up to that timestamp.
3. Determine whether processing or image saving will need a pseudo-frame.
4. When needed, remove expired events and add the newly received events.
5. Run the active processor if inside the processing window.
6. Save raw and processed images if inside the saving window.

Pseudo-frame construction starts one `pseudoframe_length` before the first
processing or saving window, ensuring that the first consumed frame is already
complete. Outside those windows and their warm-up periods, source events are
read and discarded without updating the pseudo-frame.

Raw-image saving is independent of the processor chain. If processing finishes,
fails, or misses a realtime deadline, pseudo-frame construction continues for
as long as the saving window still requires it.

### Frame time

Frame time is deterministic and is never calculated from how long processing
took:

```text
frame_time_us = frame_number * processing_interval_us
event_cutoff  = first_event_timestamp + frame_time_us
```

Frame number 1 therefore corresponds to one processing interval after the
first event. Integer microseconds are used internally to avoid cumulative
floating-point drift.

In realtime mode, the wall clock is used only to wait for the frame timestamp
and enforce a deadline. A processor must finish before the following frame
timestamp. If the deadline is missed, the processor chain is terminated and
the final application exit code is `1`; pseudo-frame construction and raw-image
saving continue until the configured saving window ends.

Offline playback does not enforce wall-clock deadlines because its simulated
event time is deliberately independent of CPU speed.

### `CircularBuffer<T>`

`CircularBuffer`, defined in `include/utils/circular_buffer.hpp`, stores events
contributing to the current pseudo-frame in timestamp order. It supports FIFO
`append`, `peek`, `pop`, and `clear` operations. It grows when full, shrinks when
mostly empty, and refuses to allocate more than 1 GiB for its element array.

## Frame processors

### `ProcessResult`

Every frame processor returns one of three values:

| Result | Effect |
|---|---|
| `OK` | Keep this processor active for the next frame. |
| `DONE` | Terminate it and activate the next processor on the following frame. |
| `ERR` | Disable the complete processor chain and mark the run as failed. |

### `FrameProcessor`

`FrameProcessor` is the base class for algorithms operating on pseudo-frames.
`Processing` supplies it with the image dimensions and current pseudo-frame.
Derived classes implement:

```cpp
void on_setup();
ProcessResult process_frame(unsigned framenum);
void on_termination();
```

`on_setup()` is called when the processor becomes active, not when it is added
to the list. `on_termination()` is called when it returns `DONE` or `ERR`, misses
a deadline, reaches `processing_end`, or the event loop terminates.

Protected helpers provide pseudo-frame access:

```cpp
PS(x, y);          // one signed pseudo-frame pixel
PS(index);         // the same operation by linear index
get_raw_frame();   // the complete BasicFrame<std::int16_t>
```

The inherited `events()` accessor exposes the batch read for the current frame.
None of the existing processors uses it; it is only relevant to a future
algorithm that needs individual event timestamps rather than just the assembled
pseudo-frame.

A processor may create an `ImageSaver` in `on_setup()`. `Processing` saves that
image alongside the raw image when the processor runs on a saving frame.

### `DummyProcessor`

`DummyProcessor` performs no analysis and always returns `OK`. It is useful for
testing pseudo-frame construction and raw-image output without an algorithm.

### `Proc1`

`Proc1` is the earliest experimental processor. It searches for and follows a
horizontal event line near a moving `(x, y)` seed. It stores its diagnostic
image in a flat `std::vector<float>` and writes displacement-related output to
`data/displ.csv`.

### `Proc2`

`Proc2` is the first correlation-based specimen tracker. It:

- removes isolated hot pixels;
- smooths the pseudo-frame with a weighted 3x3 filter;
- tracks rectangular blocks against a reference image using squared error;
- updates its reference after enough vertical movement;
- searches for the specimen outline and crack.

It stores frames in flat vectors and writes its principal results to
`data/proc2out.csv`.

### `Proc2b`

`Proc2b` is the current processor selected in `main.cpp`. It is a revision of
`Proc2` that uses the `Frame` class and separate top and bottom tracking blocks.
Its main stages are:

1. Copy and clean the signed pseudo-frame.
2. Apply a weighted 3x3 smoothing filter.
3. Correlate tracking blocks with the reference frame to follow specimen motion.
4. Detect black and white specimen edges and periodically update DIC references.
5. Measure edge separation across five blocks.
6. Fit a line to those measurements and extrapolate its zero crossing as the
   crack x-coordinate.

It writes crack positions to `data/proc2out.csv`, tracking diagnostics to
`data/proc2out2.csv`, and creates an annotated red overlay for images saved in
`proc_images/`.

`Proc1`, `Proc2`, and `Proc2b` currently return `OK` on every successful frame.
To use them as phases in a longer chain, add the phase-completion condition and
return `DONE` when that condition is met.

The correlation processors use the shared `DicBlock` type to keep the reference
and current position of a rectangular image region. `DicFlapPair` groups the
top and bottom blocks used around the specimen.

## Image and mathematical helpers

### `BasicFrame<T>`

`BasicFrame<T>` owns a two-dimensional image in row-major order. It provides
dimensions, checked and unchecked pixel access, linear indexing, iteration,
fill/reset operations, and access to the underlying contiguous data. Supported
pixel types are `std::int16_t`, `int`, `float`, and `double`.

The raw pseudo-frame is a `BasicFrame<std::int16_t>`, which preserves exact
signed event counts without paying for floating-point storage.

### `Frame`

`Frame` derives from `BasicFrame<float>` and inherits its constructors. It is
the processed-image type and adds `find_hline()` and `find_specimen()`. Keeping
these operations on `Frame` prevents integer frames from exposing algorithms
whose scores and fitted output are inherently floating point.

`find_hline()` searches a rectangular area for a horizontal black or white
event edge. It chooses a y-coordinate for every x-coordinate and fits a straight
line through those points.

`find_specimen()` searches multiple image columns for compatible black and
white boundary lines and returns the consensus specimen position when enough
columns agree.

### `Point` and `Line`

These small structures, defined in
`include/img_processing/datatypes/geometry.hpp`, describe a point and a line.
`Line::eval(x)` evaluates the fitted y-coordinate, and `Line::Type` records
whether the line represents a white, black, or unrestricted edge.

### `ImageSaver`

`ImageSaver` converts application pixel values into 24-bit BGR images using a
caller-provided color function. It can write BMP directly or PNG through
OpenCV. `Processing` owns the raw pseudo-frame saver, while each frame processor
may own a saver for its diagnostic image.

### `timing::Timer`

The functions in `timing` return monotonic microseconds or milliseconds since
an arbitrary process-local origin. `Timer` stores a reference point and measures
elapsed time from it. This clock is suitable for intervals and deadlines, not
calendar timestamps.

### Other helpers

- `BestN`, in `include/utils/best_n.hpp`, retains a fixed number of the
  highest-ranked values.
- The grouping helpers in `include/utils/grouping.hpp` select mutually
  compatible candidates and the largest candidate groups.

## Selecting and ordering processors

Processors are added in `main.cpp` in execution order:

```cpp
Config config = load_config("config/config.cfg");
Playback playback("data/test2.raw", config.realtime);

Processing application(playback, config);
application.add_processor(std::make_unique<Proc2b>(config));
return application.run();
```

For live capture with the interactive console:

```cpp
Config config = load_config("config/config.cfg");
EventCamera camera(config.camera_config_path);

Processing application(camera, config);
application.add_processor(std::make_unique<SpecimenFinder>(config));
return application.run_console(camera);
```

The console uses these two-digit commands:

| Command | Action |
|---|---|
| `00` | Print current state. |
| `10` / `11` | Start / stop EVT3 recording. |
| `20` / `21` | Start / stop pseudoframe assembly and saving. |
| `30` | Start processing immediately (and start pseudoframes if needed). |
| `31` | Stop processing and processor-image saving. |
| `32` | Start processing after one complete pseudoframe window. |
| `40` / `41` | Start / stop a test (RAW recording plus delayed processing). |
| `99` | Close the console and camera. |

Existing RAW files are not overwritten: a numeric suffix is added to the
configured `raw_save_path` when necessary.
Starting pseudoframe saving clears `images/`; starting processing clears
`proc_images/`; starting a test clears both directories.
The complete state transitions and edge cases are documented in
[`docs/camera_console.md`](docs/camera_console.md).

For a multi-phase test:

```cpp
application.add_processor(std::make_unique<InitializationProcessor>(config));
application.add_processor(std::make_unique<SpecimenProcessor>(config));
application.add_processor(std::make_unique<CrackProcessor>(config));
```

Only one processor is active. When it returns `DONE`, the next processor starts
on the next pseudo-frame.

A minimal processor has this form:

```cpp
class ExampleProcessor : public FrameProcessor {
public:
    explicit ExampleProcessor(Config cfg) : FrameProcessor(cfg) {}

private:
    void on_setup() override {
        // Allocate phase-specific state here.
    }

    ProcessResult process_frame(unsigned framenum) override {
        double t = framenum * cfg.processing_interval;

        // Read the pseudo-frame with PS(x, y) or get_raw_frame().

        if(phase_is_complete(t))
            return ProcessResult::DONE;
        return ProcessResult::OK;
    }

    void on_termination() override {
        // Flush or close phase-specific output here.
    }
};
```

## Building and running

The project requires a C++17 compiler, OpenCV, and SDL2. OpenEB/Metavision is
required at runtime for both live capture and RAW playback.

```sh
make
./build/image_processor
```

The Makefile enables OpenEB automatically when its camera header is found. It
can also be controlled explicitly:

```sh
make OPENEB=1
make OPENEB=0
```

An `OPENEB=0` build succeeds, but constructing `Playback` or opening an
`EventCamera` reports that OpenEB support is unavailable.

`make run` clears `images/` and `proc_images/` before starting the application,
so it should only be used when deleting the previous generated images is
intended.

Press Enter while the application is running to stop the event loop.

## Outputs

The current application produces:

- `images/*.bmp`: raw signed pseudo-frames rendered as grayscale;
- `proc_images/*.bmp`: processed frames and red diagnostic overlays;
- `data/proc2out.csv`: time and estimated crack x-coordinate;
- `data/proc2out2.csv`: specimen-tracking and reference-update diagnostics.

Image filenames are the deterministic pseudo-frame time in milliseconds.

The scripts in `commands/` provide simple image viewing, CSV plotting, crack
post-processing, and curve-fitting utilities.

## Source layout

```text
include/videosource/       event-source interfaces
src/videosource/           live camera and RAW playback implementations
include/processing/        processor and orchestration interfaces
include/processors/        concrete processor interfaces
src/processors/            processing loop and concrete processors
include/img_processing/    frame algorithms and processor-facing data types
src/img_processing/        Frame image-processing operations
include/image_saver.hpp     BMP/PNG output
data/                       configuration, recordings, and CSV output
commands/                   viewing and analysis helpers
docs/                       additional diagrams and notes
```
