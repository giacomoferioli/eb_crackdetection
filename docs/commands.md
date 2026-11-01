# Command quick reference

Run these commands from the repository root. This page is intentionally short;
binary-layout and implementation details are in
[`ebl_technical.md`](ebl_technical.md), while live-console behavior is in
[`camera_console.md`](camera_console.md).

## Build

```sh
make                         # main image processor
make tools                   # histogram, EBL converter, and camera recorder
make event-delta-histogram   # histogram only
make evt3-to-ebl             # EBL converter only
make record-camera           # standalone camera recorder only
make clean                   # remove compiled project targets
```

The camera and RAW-file tools require OpenEB/Metavision. The Python analysis
tools require Python 3 and the packages mentioned below.

## Convert RAW/EVT3 to EBL

```sh
./build/evt3_to_ebl INPUT.raw OUTPUT.ebl
```

Example:

```sh
./build/evt3_to_ebl recordings/fatigue/fatigue2.raw recordings/fatigue/fatigue2.ebl
```

The output filename must end in `.ebl`. Conversion runs as fast as OpenEB can
decode and overwrites the output path.

## Analyze timestamp gaps

```sh
./build/event_delta_histogram INPUT.raw OUTPUT.csv
```

Example:

```sh
./build/event_delta_histogram recordings/fatigue/fatigue2.raw event_deltas.csv
```

The program groups consecutive CD-event timestamp differences into 10 us bins,
writes the nonempty bins to CSV, and prints a terminal histogram.

## Run the image-processing application

```sh
make
./build/image_processor
```

or:

```sh
make run
```

`make run` first deletes files directly inside `images/` and `proc_images/`.
Input mode, recording path, ROI, and enabled processors are currently selected
in `src/main.cpp`; timings and processing settings come from `config/config.cfg`.
To use EBL playback in `main.cpp`:

```cpp
EblitePlayback playback("recordings/fatigue/fatigue2.ebl");
Processing application(playback, config);
```

When the application is compiled in live mode, it accepts the interactive
two-digit commands documented in [`camera_console.md`](camera_console.md).

## Record a camera RAW file

```sh
./commands/record_camera OUTPUT.raw [CAMERA_CONFIG]
```

Example:

```sh
./commands/record_camera recordings/test.raw config/camera_settings.json
```

If `CAMERA_CONFIG` is omitted, the project camera configuration is used. Press
Enter or Ctrl+C to stop and finalize the recording.

## Inspect a RAW recording

```sh
./commands/recording_info INPUT.raw
```

This runs Metavision's `metavision_file_info` and prints the script's legacy
integer storage-rate calculation after the separator.

## Plot CSV columns with gnuplot

```sh
./commands/plot INPUT.csv [Y_COLUMN] [GNUPLOT_SETTINGS]
```

Examples:

```sh
./commands/plot data/proc2out.csv
./commands/plot data/proc2out.csv 3 "set title 'Displacement'"
```

Column 1 is used for X. `Y_COLUMN` defaults to 2. This command requires
`gnuplot`.

## Fit line/cubic CSV data

```sh
python3 commands/fit_piecewise_csv.py INPUT.csv OUTPUT.csv
```

The input rows are `timestamp,y0,y1,...`. This command requires NumPy.

## Inspect fitted CSV frames

```sh
python3 commands/plot_csv_frame.py FITTED.csv RAW.csv
```

Enter timestamps interactively; enter a negative timestamp to quit. This
command requires NumPy, Matplotlib, and a graphical display.

## `calc_crack.py`

`commands/calc_crack.py` is an unfinished prototype and is not currently
runnable. It is listed here so every file in `commands/` has an explicit status.
