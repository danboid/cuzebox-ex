# Hardware diagnostics and automatic failure capture

These debugger tools are **off by default**, session-only, and independent of
game-specific memory layouts. Enable them in the native debugger under
**Hardware diagnostics / failure capture**, in web Controls, or through the
localhost API. They observe emulation without changing hardware behavior.

## Observations and masks

`DIAGNOSTICS ENABLE` starts a bounded event history. `DISABLE` stops warnings
unless failure capture remains armed. `STATUS` reports counts, masks, the
`first_event` sequence still retained, recorder status, and the last bundle
path (UTF-8 encoded as `bundle_path_hex`). `READ <sequence>` returns one event.
`CLEAR` clears history while disarmed.

Events report the instruction's **word PC**, wrapping 32-bit AVR cycle, frame,
raster row/beam position, detail value, and repetition count. Source line/file
index is included when ELF source information is available; resolve the index
with `SOURCE_LIST`. Bundle events include source paths. Adjacent identical
observations at the same PC within a frame are coalesced.

| Bit / value | Observation | Detail / interpretation |
| --- | --- | --- |
| 0 / 1 | SPI write collision | Attempted byte; a write during an active, enabled transfer is rejected by the hardware. |
| 1 / 2 | Unread SPI receive | Previous receive byte overwritten before an SPDR read. Write-only traffic may intentionally do this. |
| 2 / 4 | UART overrun | Dropped loopback byte, or zero for a modeled RX deadline overrun. Uses the existing UART overrun model. |
| 3 / 8 | Stack high write with interrupts enabled | SPH value. The usual CLI / SPH / saved-SREG / SPL sequence is allowed. |
| 4 / 16 | Unusual sync interval | Cycles between falling edges outside 800–1000 or 1700–1950 cycles. A Uzebox half/full-line heuristic, not a universal television-validity check. |
| 5 / 32 | Watchdog reset | WDTCSR, observed before reset. Intentional soft resets are common. |
| 6 / 64 | Guarded memory access | Address violating an existing `WATCH GUARD`. Ordinary watchpoints do not trigger capture. |
| 7 / 128 | Replay failure | Zero; consult movie status for details. |
| 8 / 256 | Assertion failure | Caller-defined numeric ID from a harness or manual capture. |

`DIAGNOSTICS MASK <bits>` selects observations (default **511**, all).
`DIAGNOSTICS TRIGGERS <bits>` selects automatic capture triggers (default
**480**, watchdog/guard/replay/assertion). Trigger selection is independent of
the warning mask. Zero triggers enables manual-only capture. Warnings indicate
suspected problems: startup, deliberate timing tricks, discarded SPI reads,
and watchdog soft resets may all be valid cartridge behavior.

## Failure recorder

```text
DIAGNOSTICS TRIGGERS 480
DIAGNOSTICS ARM C:/game-tests/failures
RESUME
```

ARM creates the output directory if its parent exists. The rest of the command
is the path, so spaces work without quotes. Arming clears history and takes a
checkpoint. A selected trigger freezes history and pauses at the next safe
boundary (after the instruction for CPU events). It writes one new
`failure-<time>-<serial>` directory, then disarms. Existing bundles are never
overwritten. `DIAGNOSTICS TRIGGER <numeric-id>` manually captures while armed,
regardless of trigger mask. `DISARM` frees checkpoints.

The bounded history retains:

- 1,024 event groups.
- 4,096 instruction starts, with PC, cycle, stack pointer, and opcode.
- 1,024 controller latch packets with cycle, port, slot, and bit count.
  Observation coexists with movie and logical-test input hooks.
- Two frame-boundary machine checkpoints; resets and state changes clear old
  timelines. SPI RAM is limited to 16 MiB while recording and each checkpoint
  to 32 MiB. A transient third snapshot can exist during replacement/export.

No per-instruction allocations or disk writes occur. The opt-in frame-boundary
snapshot copies can reduce speed. Disabled operation takes no snapshots and
writes no files; debugger builds retain small fixed trace arrays.

Each bundle includes `manifest.json` (trigger, ROM identity/path, behavior ID,
effective video/UART/bootloader/SPI-RAM settings, SD timing, sequence boundaries,
drop counts and completion status), `before.state`, `failure.state`,
`flash.bin` (64 KiB), `events.jsonl`, `execution.jsonl`, `inputs.jsonl`, and
`screen.bmp` when framebuffer capture is available.

The failure snapshot is taken after the triggering instruction; for watchdog
reset it is post-reset. The event and prior checkpoint retain pre-reset context.
The screenshot is the most recently rendered display and can precede a mid-frame
trigger. Export failures still pause/disarm and preserve partial output. Trust
a bundle only when its manifest exists and `complete` is true.

## Restore and reproducibility

Load the same ROM and pause, stop movies/logical tests, then:

```text
DIAGNOSTICS DISARM
DIAGNOSTICS RESTORE C:/game-tests/failures/failure-.../before.state
```

RESTORE checks ROM identity, behavior ID, size, checksum, and savestate structure
before changing the machine. Snapshot ABI requires a compatible build/architecture.

These are **diagnostic bundles**, not self-contained deterministic movies.
External SD backing files, live network traffic, keyboard events, logical-runner
bookkeeping, and movie cursors are not archived. Bounded history can be shorter
than the checkpoint interval; `replay_complete` is explicitly false. Keep the
original ROM/SD fixtures, ELF, `.cic` capture, and logical-test artifacts.
The existing `.cic` mechanism remains the strict same-build replay option.
Savestate payloads can contain device configuration.

## Game harness integration

```sh
python tools/run_game_test.py game-test.json --emulator ./cuzebox \
  --output results/new-run --failure-capture
```

This optional flag arms capture and requests a bundle with ID 1 after a failed
assertion. The runner also retains its existing logical trace, checkpoint,
screenshot, and assertion report. Game-specific interpretation stays in the
game's manifest.

`tools/test_diagnostics.py <emulator>` tests real AVR sequences, disabled
operation, safe stack updates, automatic triggers, bounded history, unique
directories, corrupt/wrong-ROM restore, and output failures.
