# Logical-update development tests

Logical tests drive **logical game updates**, independently of video frames or
instruction timing. Exact-cycle `.cic` movies remain the tool for reproducing the
same cartridge build. Logical tests can reuse inputs across builds after resolving
the new build's marker symbols and memory layout.

The API commands below require a debugger-enabled build. Hooks are disabled by
default. The interpreter checks one nullable execution hook; memory ranges are
copied only at the observation marker. No socket or filesystem I/O occurs inside
that hook. Buffered records are exported separately. Game-specific interpretation
belongs in the caller, not the emulator.

For declarative tests with named fields, assertions, structured results and
automatic failure bundles, use the Python [game-test manifest runner](game-test-manifests.md).

## Timing and address rules

Both markers run **before** the instruction at their address. Use an input marker
before the kernel/game samples its controller input. Putting it at a function that
already consumes latched input will affect the *next* sample, not retroactively
change the current one. Use a distinct observation marker after the update's writes.
If both markers are identical, input advances first and then state is observed.

Program addresses are AVR **word addresses**, like `RUN_TO` and `READ_SYMBOL` for
program symbols. `CONFIG` also accepts loaded ELF program-symbol names. Symbol
addresses are resolved at configuration time. Reload symbols and configure again
after loading a changed ROM. SRAM ranges use the raw 0..4095 indices used by
`READ_MEM SRAM` (an AVR data address maps with `address & 0xfff`). SPI ranges use
decoded chip byte addresses, bounded by the configured RAM size.

All selected ranges are copied on the emulation thread at the same execution
boundary. There is no emulated instruction or device advancement between ranges.
Cycle timestamps are the AVR's wrapping 32-bit cycle counter; record indices and
update numbers establish order across a cycle wrap.

## Configure and run

```text
PAUSE
LOAD_SYMBOLS /path/to/game.elf
LOGICAL CLEAR
LOGICAL CONFIG PO_InputSample PO_UpdateDone 1 10000000 0
LOGICAL RANGE SRAM 0x200 32
LOGICAL RANGE SPI 0x12000 64
LOGICAL INPUT 0 120
LOGICAL INPUT 8 1
LOGICAL INPUT 0 120
LOGICAL START 241
RESUME
```

`PO_InputSample` and `PO_UpdateDone` are illustrative symbols; choose actual code
locations with the timing above. Command arguments:

| Command | Meaning |
| --- | --- |
| `LOGICAL CONFIG input_pc observe_pc every timeout player` | Advance input every N eligible input-marker hits. Timeout is the maximum executed instructions without an input advance. Player is 0 or 1, slot 0. |
| `LOGICAL CONDITION SRAM\|SPI address mask value` | An input-marker hit is eligible when `(byte & mask) == value`. Ineligible hits do not advance the hit counter. |
| `LOGICAL CONDITION OFF` | Remove the condition. |
| `LOGICAL RANGE SRAM\|SPI address length` | Append a range to each observation, in declaration order. `SPIRAM` is an alias for `SPI`. |
| `LOGICAL INPUT mask updates` | Append an active-high SNES button mask, held for this many input advances. |
| `LOGICAL START observations` | Reset cursors and records; arm the hooks. A zero target runs until input exhaustion, timeout, or buffer exhaustion. Does not resume a paused emulator. |
| `LOGICAL CONTINUE observations` | Resume the current logical cursor, retaining observations. The target is an absolute record count, not an additional count. |
| `LOGICAL STATUS` | Active flag, terminal reason, logical update count, observation count, byte width and input cursor. |
| `LOGICAL READ index` | One record with update, PC, cycle, raster row/cycle and concatenated bytes encoded as hex. |
| `LOGICAL EXPORT path` | Write a new JSONL file containing schema/configuration metadata followed by all buffered observations. Paths may contain spaces; existing files are not overwritten. |
| `LOGICAL STOP` | Disarm hooks and release the controller override, retaining records. |
| `LOGICAL CLEAR` | Stop, discard configuration/records and free logical checkpoints. |

Use `START`, then `RESUME` or `RUN_FRAMES`. Normal observations do not stop the
emulator. Reaching the target, timeout, input exhaustion, or a full buffer pauses
at an instruction boundary and reports a terminal reason. The last input remains
effective until the next eligible advance; exhaustion stops before executing that
next input marker. Holding an input is independent of video frames and physical
controller changes. Slot 0 should be a standard pad; keyboard/mouse routing is not
overridden. Movies and netplay cannot be started through this interface concurrently
with logical testing. External devices are not recorded by logical traces.

Limits are 4096 input entries, 16 ranges, 1024 total bytes per observation and 4096
observations. A full buffer **stops with `buffer full`**, rather than overwriting
unread observations. Export includes the terminal reason; inspect it when deciding
whether a run completed. Configuration changes require `CLEAR` once records exist.
ROM loading/reset clears logical configuration and checkpoints. A plain savestate
restore cancels the hook because it cannot restore a logical cursor.

## Repeat a short interval

Logical checkpoints include four in-memory slots, independent of movie checkpoints,
and persistent `LOGICAL CHECKPOINT SAVE|LOAD <path>` files. See
[persistent checkpoints and SD fixtures](game-test-advanced.md) for file identity,
storage, and runner resume rules.
Pause/stop first. They contain machine state, input sequence and cursor, hit/timeout
counters, observation cursor and buffered observations, plus a mounted logical
raw SD image when present. They do not record external
network peers or host-device state.

```text
LOGICAL CHECKPOINT TAKE 1
LOGICAL CONTINUE 1000
RESUME
# After the target pauses execution:
LOGICAL CHECKPOINT RESTORE 1
LOGICAL CONTINUE 1000
RESUME
LOGICAL EXPORT repeat.jsonl
```

Restoration leaves the emulator paused. Continuing past a stop at an observation
does not capture that same boundary a second time. To inspect update 948, stop at
an earlier observation count, checkpoint it, then continue to the desired count.
Input updates and observations are separate counters; choose markers that occur
once per logical update if a one-to-one mapping is needed.

`tools/compare_logical_traces.py expected.jsonl actual.jsonl` reports the first
differing byte, its range/address, and update. It intentionally ignores PC/cycle
differences between builds. A project-specific comparator can map those bytes to
health, poses, RNG or script fields.

## SPI RAM watchpoints and guards

```text
WATCH SET 0 SPIRAM W 0x12000 0x12fff
WATCH GUARD 1 SPIRAM RW 0x10000 0x1ffff
WATCH HIT
WATCH HIT CLEAR
WATCH CLEAR ALL
```

`SET` breaks on accesses **inside** the inclusive range. `GUARD` breaks on accesses
**outside** its inclusive allowed range. Guards share the eight existing watch
slots. `WATCH LIST` identifies guard entries. SRAM and I/O also accept guards.
SPI addresses retain all bits, including addresses above 64 KiB.

These hooks observe decoded RAM byte reads/writes, not command/address bytes.
Reads are reported when the chip model fetches the first/prefetched byte; the
corresponding transfer may clock that byte out later. Page/sequential wrapping
uses the actual masked chip address. API inspection and snapshot copying do not
generate device accesses. Hits report address, value, originating AVR instruction
PC, transaction cycle and raster row/cycle. The CPU stops after the issuing
instruction, preserving normal transaction behavior. `MEM_TRACE` can collect the
same SPI accesses without stopping; its existing bounded ring/sequence interface
applies. Disabled tracing/watchpoints install no SPI callback.

## Exact movie seek and diagnostics

See [the movie format](input-movie-format.md) for versions and digest definitions.

```text
INPUT_MOVIE PLAY /path/to/recording.cic
PAUSE
INPUT_MOVIE CHECKPOINT TAKE 1
INPUT_MOVIE SEEK 948
# Poll INPUT_MOVIE STATUS and RUN_STATUS until paused.
INPUT_MOVIE CHECKPOINT RESTORE 1
```

Movie seek counts recorded `FRAM` boundaries, **not logical updates**. Slot 0 is
automatically captured at replay start; slots 0..3 may be replaced explicitly.
Seek restores the nearest saved checkpoint at or before the target and replays
with all strict checks enabled. It pauses at the target; seeking beyond END is
rejected. Checkpoints can also be restored after successful EOF or a mismatch.
They include the next event, file offset, partially decoded compact block, frame
and cycle counters, SD write digest, private sector cache and topology bookkeeping,
in addition to machine state. Restoring machine state alone is insufficient.

Checkpoints are process-local, retain memory until a new movie session or shutdown,
and require the same ROM identity and unchanged movie bytes. File checksums are
rechecked on restore. Taking an additional checkpoint is explicit and may pause for snapshot
allocation and file hashing; it is not performed in a per-instruction hook.
Starting a new recording/replay discards the previous movie's checkpoints. Video
recording and netplay must be stopped before seek/restore.

## Regression commands

```sh
python3 tools/test_logical_api.py /path/to/cuzebox
python3 tools/test_input_movie_api.py /path/to/cuzebox
make TSYS=linux OBD=_obj_/review/linux OUT=_obj_/review/linux/cuzebox \
  _obj_/review/linux/development_regression _obj_/review/linux/movie_regression
_obj_/review/linux/development_regression
# Run movie_regression in a disposable working directory; it writes fixtures.
```

The tests execute real AVR marker loops and controller latches, checkpoint/restore
observations, enforce conditions/timeouts, exercise SPI reads/writes above 64 KiB
and guarded ranges, and replay checkpoints with private SD writes. Movie API tests
also exercise compressed seek, legacy playback, behavior mismatch rejection and
SPI-only fingerprint diagnostics.


Validation on 2026-10-15: Windows and Linux passed the logical API suite (including
real AVR controller latches, frame-independent holds, overflow and checkpoint
repeatability) and movie API suite (seek, checkpoint restore, v1 playback, behavior
rejection and SPI fingerprint diagnostics). Native integration tests cover decoded
SPI guards and compressed replay checkpoints with private SD writes. These are
synthetic cartridges and protocol fixtures; Punch-Out-specific marker placement
and field interpretation must be selected against that cartridge's ELF/layout.
