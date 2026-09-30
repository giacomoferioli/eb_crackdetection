# Image processing display

A small C++17/SDL2 framework for displaying and processing `int16_t`
grayscale images in real time. It opens a raw-event window, a processed-output
window, and a settings window. Both image windows use nearest-neighbor scaling.

## Build and run

SDL2 and OpenCV development files, Zenity, and a C++17 compiler are required.
OpenCV loads PNG files and encodes MP4 recordings; Zenity provides the native
open/save file dialogs.

```sh
make
make run
make clean
```

The executable is written to `build/image_processor`; intermediate files are
written to `objs/`. The `data/` directory is available for application data.

## OpenEB and EVK4

The application automatically enables its camera backend when OpenEB and the
Prophesee camera plugin are installed. It can also be enabled explicitly with:

```sh
make clean
make OPENEB=1
make run
```

OpenEB is assumed to be installed under `/usr/local`. For another location:

```sh
make OPENEB=1 OPENEB_PREFIX=/path/to/openeb/install
```

`Processing::camera()` exposes the shared `EventCamera` instance. Open and
start it in `on_setup()`. The framework drains the camera once per frame,
renders that event batch in the raw window, and exposes the same batch through
`Processing::events()` to avoid splitting events between the two views:

```cpp
void on_setup() override {
    if (!camera().open_usb() || !camera().start()) {
        // camera().last_error() describes the failure
    }
}

void process_frame(std::int16_t* pixels, int width, int height,
                   std::uint64_t frame_number) override {
    for (const EventCamera::Event& event : events()) {
        // event.x, event.y, event.polarity and event.timestamp_us
    }
}
```

The camera callback performs no allocation, locking, display work, or logging.
It copies events into a preallocated SPSC ring capable of holding about four
million events by default. `dropped_events()` reports overflow if processing
does not drain the ring quickly enough. The class also provides `set_bias()`,
`biases()`, `set_roi()`, and `enable_roi()` for EVK4 hardware configuration.

The EVK4 main trigger input is enabled automatically when the camera opens.
Trigger edges are placed in an independent 4096-entry SPSC queue and can be
read without affecting CD events:

```cpp
std::vector<EventCamera::TriggerEvent> triggers;
triggers.reserve(256);
camera().read_trigger_events(triggers);

const std::int64_t last_rising = camera().last_rising_trigger_timestamp_us.load();
```

Each trigger contains `timestamp_us`, `channel_id`, and `polarity` (`1` for a
rising edge and `0` for a falling edge). The public atomic timestamp is `-1`
until the first rising edge arrives. `trigger_input_enabled()` reports whether
OpenEB enabled the main input, and `dropped_trigger_events()` reports queue
overflow. Cached video recordings currently contain CD events only.

## Settings layout and controls

`Processing` creates three settings columns by default. The new left column
selects the event source, the middle column contains application/display
controls, and the right column contains live camera bias/ROI controls. The
built-in display controls provide independent raw/processed zoom, synchronized
play/stop buttons, and raw-event persistence. At least three columns are always
created; a larger count can be requested with the fourth constructor argument:

```cpp
DemoProcessing()
    : Processing(1280, 720, "Image processing display", 3) {}
```

Every add-control function accepts a zero-based column as its final argument.
When no callback is needed, the short overload accepts the column directly:

```cpp
settings().add_binary_button("PERSIST", persist_, 1);
settings().add_int_slider("GAIN", gain_, 0, 100, 1);
settings().add_float_input("THRESHOLD", threshold_, 0.0F, 1.0F, 1);
settings().add_int_input("COUNT", count_, 0, 1000, 1);
settings().add_compact_button("RESET", [this] { reset(); }, 1);
```

### Event-source controls

- `LIVE` starts or resumes the EVK4 feed.
- Compact `REC` clears the previous cached clip and records live events with
  their original timestamps. Compact `STOP` ends recording or stops the current
  source.
- `REPLAY` replays the cached event clip. `REPLAY SPEED` ranges from 0.1x to
  32x and also controls the speed used when exporting the clip.
- `SAVE MP4` renders the cached event stream to a 30 FPS MP4 selected through a
  save dialog. MP4 frames use gray as the neutral background, white for ON
  events, and black for OFF events. Saving stops the active source before the
  synchronous file dialog and encode, preventing camera-ring overflow.
- `LOAD PNG` selects a PNG and continuously emits one event per pure black or
  pure white pixel on every processing frame. White is ON, black is OFF, and
  every other color is ignored.
- Compact `DUMP` writes the cached clip to an `.evbin` binary event file.
  Compact `LOAD` validates and restores one of those files into the cache; use
  `REPLAY` afterward to play it.

The recording cache stores up to 16 million events (about 256 MiB). The camera
is stopped during replay, PNG playback, and source stop so its hardware queue
cannot build up in the background. Switching back to `LIVE` restarts it.

Compact buttons are square and laid out at the bottom of their column. Integer
and floating-point input boxes are edited by clicking, typing a value, and
pressing Enter; Escape cancels editing. Input values are clamped to the supplied
range and invoke callbacks in the same way as sliders.

After `on_setup()`, the framework queries the open camera for its modifiable
biases and real supported ranges. It adds one numeric input per bias plus ROI
position, size, enable, and full-sensor controls in the last column. Pressing
Enter in any ROI field applies the complete ROI immediately; no separate apply
button is required. Both display windows and their frame buffers are resized to
the ROI dimensions at the same time.
`ROI LOCAL XY` makes returned event coordinates relative to the ROI origin. The
conversion is fused into the ring-buffer read, and events that were already in
flight outside a newly applied ROI are discarded. Turning the option off keeps
the original full-sensor coordinates.

Camera biases and hardware ROI configuration are loaded from
`data/camera_settings.json` after the camera is opened. On normal application
shutdown, the current OpenEB camera settings are saved back to that file before
the stream is stopped. The first run simply starts from the camera defaults
because the file does not exist yet.

## Adding processing code

Derive a class from `Processing`. Add UI controls in `on_setup()` and write the
processed image to `pixels` in `process_frame()`. `display()` refers to the
processed window, `raw_display()` refers to the automatic raw-event window,
and `events()` returns the current shared batch. That interface is identical
for live camera input, recorded replay, and PNG-generated events, so processing
code does not need to know which source is active. The example in
`src/main.cpp` demonstrates this processing path.

The settings controls bind to variables by reference, so those variables must
remain alive as long as the settings window. Members of the processing class
are a natural fit.

`DisplayWindow::draw()` accepts a row-major buffer containing `width * height`
pixels. Values from 0 to 255 map to grayscale; values outside that range are
clamped. Integer zoom is implemented by nearest-neighbor scaling.
