# Live camera console behavior

## Startup

`main.cpp` loads `config/config.cfg`, opens the first available event camera,
loads `camera_config_path`, enables ROI-local coordinates, and starts the camera
stream. It then registers the frame processors enabled in `main.cpp` and enters
`Processing::run_console()`. To build this mode, set `LIVE` to `true`, enable the
`videosource/event_camera.hpp` include, select the desired processors, and run
`make`.

The camera stream starts immediately, but the controllable features initially
start off:

- EVT3 RAW recording is off.
- Pseudoframe assembly and pseudoframe-image saving are off.
- Frame processing and processed-image saving are off.

Camera events are still drained while pseudoframes are off. This prevents the
camera ring buffer from filling with events the console does not currently need.

## Commands

The console accepts one two-digit command followed by Enter. It prints the full
command menu once at startup and only prints the `> ` prompt after commands.

| Command | Behavior |
|---|---|
| `00` | Prints whether recording, pseudoframes, and processing are on, off, or waiting. |
| `10` | Starts native EVT3 RAW recording. |
| `11` | Stops and finalizes the current RAW recording. |
| `20` | Clears `images/`, resets the rolling frame, and starts pseudoframe assembly and saving. |
| `21` | Stops pseudoframe assembly and saving. Because processing requires a pseudoframe, it also stops processing and processed-image saving. |
| `30` | Clears `proc_images/` and starts processing immediately. If pseudoframes are off, it also clears `images/` and starts them. |
| `31` | Stops processing and processed-image saving. Pseudoframe assembly continues. |
| `32` | Clears `proc_images/` and requests processing after one full pseudoframe duration. If pseudoframes are off, it also clears `images/` and starts them. |
| `40` | Starts a test: starts RAW recording, clears both image directories, resets the pseudoframe, and waits one full pseudoframe duration before processing. |
| `41` | Stops the test: processing, both kinds of image saving, pseudoframe assembly, and RAW recording are stopped. |
| `99` | Finalizes active work and closes the console and camera. |

End-of-file on stdin also closes the console.

## Pseudoframe assembly

The pseudoframe is a rolling event window whose duration is
`pseudoframe_length` seconds. Each positive event contributes `+1` to its pixel
and each negative event contributes `-1`. When an event becomes older than the
window, its contribution is removed.

Starting or resetting pseudoframe assembly clears the in-memory event queue and
sets every pseudoframe pixel to zero. It also restarts the image-save interval.

Command `30` processes the partial frame on the next processing-loop opportunity.
Command `32` waits until `pseudoframe_length` seconds have elapsed since assembly
started. If a pseudoframe has already been running for at least that long,
processing starts immediately. The wait measures elapsed time; it does not
require a minimum number of camera events.

## Processing

While enabled, the current `FrameProcessor` runs once every
`processing_interval` seconds. If a processor returns `DONE`, the next registered
processor becomes current. If no processor remains, or a processor returns
`ERR`, processing stops.

Command `31` pauses calls to the processor; it does not destroy or recreate the
processor. Starting again therefore preserves processor state and continues the
frame number. Starting processing again still clears `proc_images/` as requested.

The interactive console does not use `processing_start`, `processing_end`,
`saving_start`, `saving_end`, or `realtime`. Those settings belong to the
scheduled playback loop, `Processing::run()`.

## Image saving and directory clearing

When pseudoframe assembly is enabled, its bitmap is saved to `images/` every
`save_interval` seconds. When processing is also enabled and its processor owns
an `ImageSaver`, the processed bitmap is saved to `proc_images/` on the same
save tick and with the same filename.

Filenames contain milliseconds elapsed since the console started. Starting
pseudoframes resets the save interval but not the console timestamp origin.

The start commands clear output folders as follows:

- Starting pseudoframes clears every entry under `images/`.
- Starting processing clears every entry under `proc_images/`.
- Starting processing while pseudoframes are off clears both directories.
- Starting a test clears both directories.

Clearing is recursive, so subdirectories and non-image files placed inside
`images/` or `proc_images/` are also removed. The directories are recreated if
they do not exist. If a required directory cannot be cleared, that requested
feature is not started.

## RAW recording

RAW recording is performed directly by OpenEB and is independent of image
saving. The configured `raw_save_path` must end in `.raw`, the camera must be
running, and its native encoding must report EVT3.

An existing RAW capture is never deleted or overwritten. If the configured path
already exists, the console tries the same filename with `_1`, `_2`, and so on.
The parent directory is created when necessary.

Starting pseudoframes or processing does not start RAW recording. Only commands
`10` and `40` do that. Starting a test while recording is already active reuses
the active recording; stopping the test then stops that recording.

## Shutdown and errors

Normal console shutdown calls the active processor's termination hook, finalizes
an active RAW file, and then stops the camera. A recording or directory error is
printed in the console. The executable returns a failure status if processing
stopped because a processor returned `ERR`.

The implementation is primarily in:

- `Processing::run_console()` in `src/processors/processing.cpp`;
- EVT3 recording control in `src/videosource/event_camera.cpp`;
- live-console selection in `src/main.cpp`.
