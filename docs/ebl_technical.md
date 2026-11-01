# EBLITE (EBL) technical reference, version 1.0

EBLITE is a little-endian event-camera recording format optimized for fast,
sequential decoding. Multi-byte integers and IEEE-754 binary32 values are
stored least-significant byte first. All offsets are relative to the beginning
of the file. The standard abbreviation is **EBL**, and files use the `.ebl`
extension.

## Fixed header

The file begins with this 32-byte prefix:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | Magic bytes `EBLITE\r\n` |
| 8 | 2 | Major version (`1`) |
| 10 | 2 | Minor version (`0`) |
| 12 | 4 | Header flags; zero in version 1.0 |
| 16 | 8 | Byte offset at which the event stream starts |
| 24 | 4 | Number of metadata entries |
| 28 | 4 | Header CRC-32/ISO-HDLC |

The CRC covers every byte from offset zero up to, but excluding, the event
stream. Bytes 28 through 31 are treated as zero while calculating it.

Version 1.0 writers use event-stream offset 128. The unused bytes between the
last metadata entry and the event stream are zero. The event stream is always
aligned to at least four bytes.

## Metadata entries

Metadata entries immediately follow the fixed prefix:

| Size | Field |
|---:|---|
| 1 | Key |
| 1 | Value type |
| 2 | Entry flags; zero in version 1.0 |
| 4 | Value length in bytes |
| `length` | Value |
| 0–3 | Zero padding to a four-byte boundary |

Unknown keys can be skipped using the value length. Duplicate required keys are
invalid. Keys 0 and 128–255 are currently reserved.

### Value types

| ID | Meaning |
|---:|---|
| 1 | Unsigned 32-bit integer |
| 2 | Signed 32-bit integer |
| 3 | IEEE-754 binary32 float |
| 4 | Unsigned 64-bit integer |
| 5 | UTF-8 bytes without an implicit terminator |
| 6 | Arbitrary bytes |

### Version 1.0 keys

| Key | Type | Size | Required | Meaning |
|---:|---:|---:|---|---|
| 1 | 4 | 8 | Yes | Recording duration in microseconds: final timestamp minus first timestamp |
| 2 | 4 | 8 | Yes | Number of CD events in the event stream |
| 3 | 1 | 4 | Yes | Sensor or coordinate-space width |
| 4 | 1 | 4 | Yes | Sensor or coordinate-space height |

An empty recording has event count and duration both equal to zero.

## Events

Every event starts with one 32-bit word. Bit 31 determines whether another
32-bit word follows.

| Bits | Relative event (`bit 31 = 0`) | Full event (`bit 31 = 1`) |
|---:|---|---|
| 31 | `0` | `1` |
| 30 | Polarity | Polarity |
| 29–19 | X coordinate | X coordinate |
| 18–9 | Y coordinate | Y coordinate |
| 8–0 | Microseconds since previous event | Absolute timestamp bits 0–8 |
| Next word | Absent | Absolute timestamp bits 9–40 |

The first event must be full and have timestamp zero. Subsequent timestamps are
nondecreasing. Writers use a relative event when the timestamp delta is at most
511 microseconds and a full event otherwise. Readers must also accept a full
event when a relative event could have represented the same timestamp.

The timestamp range is 0 through `2^41 - 1` microseconds, approximately 25.45
days. X ranges from 0 through 2047 and Y ranges from 0 through 1023. Coordinates
must also be smaller than the width and height declared in the header.

The event stream contains exactly the event count declared in the header.
Version 1.0 does not define data following those events.

## Encoder behavior

`eblite::Writer` accepts timestamp-ordered `Event` arrays. It normalizes the
first input timestamp to zero, writes the initial full event, and uses relative
events for every subsequent delta from 0 through 511 microseconds. Larger gaps
produce full events.

The writer uses a 4 MiB output buffer and retains no event history beyond the
previous timestamp. It initially writes a valid empty header, streams the event
words, then seeks back during `finalize()` to store the final event count,
duration, and header CRC. Consequently, version 1.0 output must be seekable; a
pipe is not a valid destination.

The command-line encoder attaches the writer to an OpenEB CD callback:

```text
build/evt3_to_ebl INPUT.raw OUTPUT.ebl
```

OpenEB timestamp shifting does not affect the result because the writer always
normalizes against the first decoded event. External-trigger events are not
stored in EBL version 1.0.

## Playback behavior

`EblitePlayback` implements the project's `EventSource` interface. Construction
validates the magic, major version, flags, metadata, padding, CRC, dimensions,
and minimum possible event-stream size. Events are decoded synchronously from a
4 MiB byte buffer without OpenEB or a decoder thread.

The timestamp stored in an EBL file starts at zero. On the first decoded event,
the playback class adds the current monotonic-clock value, matching the timestamp
domain expected by `Processing` and by the existing OpenEB playback class.
`read_events()` retains at most one decoded look-ahead event when it lies after
the requested cutoff.

```cpp
#include "videosource/eblite_playback.hpp"

EblitePlayback playback("recordings/test.ebl");
Processing processing(playback, config);
```

The metadata accessors `event_count()` and `recording_duration_us()` do not scan
the event stream.

## Implementation files

| Component | Location |
|---|---|
| Format constants and metadata API | `include/eblite/format.hpp` |
| Header encoding and validation | `src/eblite/format.cpp` |
| Streaming event writer | `include/eblite/writer.hpp`, `src/eblite/writer.cpp` |
| EBL `EventSource` implementation | `include/videosource/eblite_playback.hpp`, `src/videosource/eblite_playback.cpp` |
| EVT3/RAW conversion tool | `tools/evt3_to_ebl.cpp` |

## Version 1.0 limitations

- No chunk table or random-seek index.
- No event-stream checksum; only the header has a CRC.
- No external-trigger stream.
- A recording interrupted before `finalize()` retains the initial empty header.
- Maximum geometry is 2048 by 1024.
- Maximum normalized duration is `2^41 - 1` microseconds, about 25.45 days.
- Event words are not independently self-synchronizing after byte corruption.

Independent chunks with per-chunk timestamps, event counts, byte lengths, and
checksums are the intended next extension for seeking, parallel decoding, and
corruption containment.
