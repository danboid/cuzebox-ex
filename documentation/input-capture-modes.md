# Compact and Full Debug capture modes

Implemented in Tools -> Input Files and the INPUT_MOVIE API. Compact is the
saved default (`InputMovieCompact=1`). Full Debug uses `InputMovieCompact=0`.
The GUI mode button toggles the preference. API clients use
`INPUT_MOVIE MODE COMPACT` or `INPUT_MOVIE MODE DEBUG` before recording.
Changing the preference during a session affects only subsequent recordings.

## Shared guarantees

Both modes preserve the identical logical event stream: all eight stable P1/P2
slots, every cycle-timestamped latch (including multiple latches in a frame),
Super Mouse packets, keyboard responses and empty polls, topology changes and
same-topology protocol resets. Both retain every frame's CPU and SD digest.
Compact does not reduce input sampling or omit debug checks.

Both include ROM identity, a full HERE or RESET starting snapshot, and initial
data for every touched SD sector. The private SD overlay, 8 MiB sector cache
limit, host input isolation, clean END!/EOF handling, exact video frame count,
settings restoration, and post-session save protection are unchanged.

## Storage and compatibility

Full Debug writes the existing uncompressed CZINPUT v1 chunks for direct
inspection. Compact writes CZINPUT v2 with independent 64 KiB LZ blocks,
compressing the snapshot and events. Incompressible blocks are stored raw.
The implementation has no external codec dependency, uses a bounded single
candidate hash search, and allocates 147,464 bytes of transport workspace per
active reader/writer. It never buffers an entire movie. This is additional to
the existing snapshot/chunk buffers and SD cache. Preflight uses its own reader
and releases it before starting replay, preserving an existing active session
when a newly selected file is invalid.

Playback detects the mode from the file header, regardless of the recording
preference or filename extension. Existing versioned .cap/.cic v1 movies remain
supported, subject to the existing snapshot ABI, ROM identity and timing checks.
Raw legacy two-byte-per-frame .cap files remain unsupported. Older builds cannot
open Compact v2 movies; select Full Debug to produce the older container format.

Every block has bounded expanded/stored lengths and a CRC. The entire logical
movie is preflighted before restoring machine state. See the
[binary format](input-movie-format.md) and [user instructions](input-files.md).

## Validation

The movie regression runs both modes through eight-slot Super Mouse/multitap,
keyboard, hot-swap/protocol reset, subframe timing, cycle wrap, private SD,
ROM mismatch, checksum corruption, truncation, desync and exact EOF scenarios.
Storage tests cover random/repetitive blocks, block boundaries, raw fallback,
malformed references/lengths/CRCs, truncated payloads and flush failures.
GUI tests exercise mode selection and replay/video finalization. Configuration
round-trip tests cover the saved Full Debug choice and Compact default. The
headless API suite checks both modes, automatic detection, invalid-file state
preservation and exact final CPU state.

## Desktop measurements (2026-10-15)

Linux/WSL synthetic 36,000-frame runs (ten minutes at 60 Hz), full UzeTap wire
protocol and a keyboard poll each frame, with a roughly 0.71 MiB snapshot:

| Slot polls per port/frame | Full Debug | Compact | Reduction | Recording CPU seconds, Debug / Compact |
|---|---:|---:|---:|---:|
| 2 | 38.61 MiB | 11.49 MiB | 70.2% | 1.480 / 1.558 |
| 4 | 85.30 MiB | 24.10 MiB | 71.7% | 2.236 / 2.348 |

These are one-run synthetic recording measurements, not full-game emulation
speed or physical handheld measurements. They exercise the capture hooks and
checks with advancing CPU-cycle counters. An idle 256-frame API fixture was
750,314 bytes in Full Debug and 26,060 bytes in Compact on Linux. Compression
ratios depend on snapshot contents, input traffic and touched SD sectors.

Windows/MinGW and Linux capture, storage, GUI, preference and API checks passed,
as did the compile-time headless API suite and browser build/smoke test (browser
capture remains disabled). Codec tests passed AddressSanitizer and
UndefinedBehaviorSanitizer, including 10,000 malformed token sequences. An
independent Python decoder expanded Compact files back to v1; replay reached
the same final CPU state. Physical handheld performance has not been measured.
The codec prioritizes bounded portable code over maximum compression ratio.
