# UZE UART connection metadata

Header byte `0x19B` describes UART connection types separately from the original
peripheral bytes. The 512-byte header size and existing offsets are unchanged.

| Bits | Meaning |
| --- | --- |
| 0 | UzeLink supported |
| 1–3 | Reserved for future UART support flags |
| 4 | UzeLink default |
| 5–7 | Reserved for the corresponding future UART defaults |

Zero means unspecified, as in existing ROMs. `uzelink=support` (also accepted as
`uzelink=supported`) sets bit 0. `uzelink=default` sets bits 0 and 4. Packrom rejects
UzeLink default combined with either `esp8266=default` or `esp8266_ap=default`,
including conflicts inherited when patching an existing UZE. Supporting both is
valid. These flags do not specify a server, pairing token, baud rate, or protocol.

CUzeBox reads, displays, and preserves this metadata when exporting a UZE.
The default UART selection for new configurations is **.UZE HINT** (`SerialRoute=7`).
It selects ESP MODULE for an ESP8266 or ESP8266 AP default, or TCP SERIAL for a
UzeLink default, using the saved TCP endpoint/mode settings. A missing default,
support-only flags, a HEX file, or conflicting defaults select DISCONNECTED.
Explicit UART selections override hints and existing saved selections are preserved.
The hint selection is saved separately from its effective route, so loading another
ROM re-evaluates the hint without changing the user's preference. The Serial API
reports both `route` (effective) and `route_selection` (preference).

The original peripheral bits now match packrom: Mouse=1, Keyboard=2,
Light Gun=4, Multitap=8, SPI RAM=16, ESP8266=32, ESP8266 AP=64.
Existing packrom-generated ROMs need no repacking. ROMs written with the
emulator's former incorrect Multitap=4 or ESP8266=8 assignments are ambiguous
and should be repacked with accurate properties; no heuristic reinterpretation
is applied.

Optional integration checks (requires an external packrom build and HEX):

```sh
gcc -std=gnu17 -DHEADLESS -ffunction-sections -fdata-sections -I. \
  tests/uze_uart/header_test.c cu_ufile.c -Wl,--gc-sections -o /tmp/uze-header-test
python3 tests/uze_uart/test_packrom.py /path/to/packrom \
  /tmp/uze-header-test /path/to/game.hex
```


The Serial window shows the resolved route, for example `.UZE HINT -> TCP SERIAL`.
Choose **CUSTOMIZE THIS GAME** before changing its UART selection to keep a
per-game override. **USE GLOBAL SETTING** removes that override. Profiles are
stored as `profiles/uart-<program-CRC>.cfg`; identical program contents share a
profile even after a rename. Only the UART selection is per-game; TCP endpoints
and other transport settings remain global. A profile can itself select .UZE HINT.
The browser Serial panel exposes the same controls. `SERIAL_STATUS` reports
`game_override`, and `SERIAL_SET GAME_OVERRIDE 1` / `0` enable/clear it.
