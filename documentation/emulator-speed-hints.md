# Loading-speed hints

Games can request temporary acceleration in CUzeboxEX without touching hardware
registers. Copy `extras/cuzebox_speed.h` into the game and use:

```c
#include "cuzebox_speed.h"

cuzebox_speed_fast();
load_assets();             /* Loading screen / progress bar. */
cuzebox_speed_normal();    /* Also call on every error/cancel exit. */
```

These calls are absolute requests, not a nesting counter. `normal` removes the
game's acceleration request and restores the user's pacing preference. It does
not force a limiter on if the user has explicitly chosen unlimited speed.

On an ATmega644, each call is a single two-cycle RJMP over eight bytes of data:
no registers, SREG bits, SRAM, peripheral registers or pins are written. Interrupts
remain enabled if they were enabled before the call. Hardware and older emulator
versions simply execute the jump. Each call occupies ten flash bytes. Non-AVR
host builds of the helper are no-ops.

## Uzebox kernel API

The updated Uzebox kernel exposes `EmulatorSpeedFast()` and
`EmulatorSpeedNormal()` through `<uzebox.h>`. They emit the same markers as the
standalone helper above, so games using that kernel do not need to copy
`extras/cuzebox_speed.h`. Both commands inline; no kernel object file or linker
change is required.

## Marker format

Words are stored little-endian in AVR flash:

| Word | Value | Meaning |
|---|---|---|
| 0 | `C004` | RJMP over the following four words |
| 1 | `4355` | Magic (bytes `55 43`) |
| 2 | `4558` | Magic (bytes `58 45`) |
| 3 | `0001` or `0000` | Request fast / normal |
| 4 | `FFFE` or `FFFF` | 16-bit complement of command |

All words must match. Unknown commands, damaged markers and payloads crossing
the flash boundary are ignored; their RJMP still executes normally. Recognition
is independent of the printf-whisper build option. The existing `0x39`/`0x3A`
logging interface is unchanged. The predecoder gives candidate jumps their own
internal opcode, so ordinary instructions need no new runtime marker check.

## Pacing behavior

The request takes effect at the next emulated frame boundary. Only host pacing
changes: CPU clocks, SPI/SD timings and other emulated device timing do not.
Accelerated audio is discarded, the audio queue is flushed on transitions, and
display refreshes are limited while emulation runs ahead. This also works in
headless runs. Browser builds use bursts of at most eight frames with an
approximately eight-millisecond budget (checked between frames) before yielding
to the browser. Host speed and browser scheduling determine the actual speedup.

Pause, frame stepping, video recording, rollback netplay, and external serial/TCP
UART routes suppress acceleration. The internal ESP module and loopback routes
are allowed. Use hints around loading, not gameplay, real-network waits or
wall-clock timeouts: external servers and hardware do not accelerate with the
emulator. Reset (including watchdog reset), ROM reset and savestate restoration
clear the request; it is host presentation state, not part of the savestate ABI.
A restore into the middle of loading therefore continues at ordinary speed.

## Validation

`make TSYS=linux OBD=_obj_/speed-hint OUT=_obj_/speed-hint/cuzebox
_obj_/speed-hint/speed_hint_regression` (on one line) builds the native regression.
Run `_obj_/speed-hint/speed_hint_regression`. It covers both requests, malformed
payloads, unrelated jumps, two-cycle execution with preserved registers/SREG,
reset/restore, pause/netplay gates, and preservation of the user's limiter choice.

The browser smoke test is `node tools/test_web_speed.js _obj_/speed-web` after
building that output directory with `TSYS=emscripten`; it needs Playwright
(`WEB_BROWSER_CHANNEL=msedge` selects installed Edge). It loads synthetic AVR
programs with fast and normal markers and measures the frame counts, without any
game assets. On the development machine it produced 49 / 98 / 49 frames in
800 ms for normal / fast / restored pacing. Speed ratios are host dependent.

`python3 tools/test_speed_helper.py` requires avr-gcc and avr-objcopy and checks
the linked AVR helper byte-for-byte, including the jump distances.
