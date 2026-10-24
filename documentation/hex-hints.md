# .hex hints

HEX programs can request display flags, SPI RAM capacity, SD timing, and UART
peripherals without a UZE header. UZE programs use their header; the emulator ignores configuration
markers when a UZE is loaded. Speed hints work in both HEX and UZE. No ROM scanning or high-resolution autodetection is used.

The Uzebox kernel defines these helpers in `uzebox.h`, beside the debug
whisper port declarations.
For other AVR projects, include `extras/cuzebox_hex_hints.h`.

```c
EmulatorHint(EMU_HINT_AREA_WEIGHTED, 1);
EmulatorHint(EMU_HINT_INTERLACE, 1);
EmulatorHint(EMU_HINT_WEAVE, 0); /* field presentation */
EmulatorHint(EMU_HINT_SPI_RAM, 2); /* 128 KiB, before SPI RAM initialization */
EmulatorHint(EMU_HINT_SD_TIMING, 3); /* fast card, before SD initialization */
EmulatorHint(EMU_HINT_ESP8266, 1); /* connect emulated ESP before UART initialization */
```

`EmulatorHint(option, value)` changes one option. SPI RAM accepts bank counts,
SD timing accepts 0..3, and all other options accept 0 (off) or 1 (on).
Arguments must be compile-time constants; unknown options
and invalid values are rejected at compile time. Each call occupies ten bytes.

`EmulatorHint(EMU_HINT_SPI_RAM, banks)` selects 0, 1, 2, 4, 8, 16, 32, 64, or 128 banks of
64 KiB. Zero disables SPI RAM. Set it before initializing or relying on the
RAM: the request resets its transaction state and updates its capacity before
the following AVR instruction executes. RAM contents after resizing must not
be relied on. This configures emulation; it does not detect or add physical RAM.

`EmulatorHint(EMU_HINT_SD_TIMING, value)` matches the `sd=` UZE header hint:
0 clears the request (NORMAL fallback), 1 selects SLOW, 2 NORMAL, and 3 FAST.
Call it before SD initialization. It changes the card timing model without
resetting the card or changing its contents. Explicit timing presets and custom
timings take precedence; the hint does not change the saved selection.

`EmulatorHint(EMU_HINT_ESP8266, 1)` requests the emulated ESP UART route;
zero clears that request. It follows the UART `.UZE HINT` policy and
preserves explicit global or per-ROM UART selections. Issue it before UART
initialization or ESP commands. Switching the route can interrupt an active
connection. It does not select firmware, modify Wi-Fi credentials, or enable
SoftAP; those keep their existing settings. Full ESP emulation must be compiled
in; this hint does not add AT/Wi-Fi support to the browser build.

`EmulatorHint(EMU_HINT_UZELINK, 1)` requests the TCP serial link used by
`uzelink=default`; zero clears that request. It uses the user's configured
endpoint, role and connection settings, including any configured listener.
It does not change the host, port or role.
Explicit global and per-ROM UART selections still win.

`EmulatorHint(EMU_HINT_ESP8266_AP, 1)` requests both the ESP UART route and
SoftAP support, matching `esp8266_ap=default`. Zero clears this AP request;
a separate ESP8266 request remains effective. The user's explicit SoftAP
enable/disable setting takes precedence over the AP hint. This permits the
firmware's AP commands; it does not choose an SSID/password or issue AT commands.

ESP8266/AP and UzeLink requests are independent flags. If both ESP and UzeLink
are requested, automatic UART selection resolves to disconnected, just as with
conflicting UZE defaults. Clear the old request before selecting another route.
For example, clear both `EMU_HINT_ESP8266` and `EMU_HINT_ESP8266_AP` before
enabling `EMU_HINT_UZELINK`. The order of conflicting requests does not change
their result. With no requests, automatic UART selection is disconnected.

`EmulatorHint(EMU_HINT_SPEED, 1)` requests unthrottled loading; value 0 restores
the user's pacing preference. Always restore normal pacing on error/cancel
paths too. Speed hints retain the existing `C004 4355 4558 0001 FFFE` and
`C004 4355 4558 0000 FFFF` encodings, so existing emulator support is sufficient.
`EMU_HINT_SPEED` is API option 0x82; the helper emits the speed format rather
than a configuration command. See [loading-speed hints](emulator-speed-hints.md)
for pause/netplay gates and reset/restore behavior.

Explicit GUI/config overrides win. `.UZE HINT` selections also follow HEX
hints when running HEX. Hints do not change saved preferences. CPU/watchdog
reset clears them; HEX starts with nearest sampling, no interlace, and the
ordinary 128 KiB SPI default, NORMAL SD timing, disconnected UART and no SoftAP
unless overridden. Issue hints again at startup.

Each helper emits ten bytes: an RJMP over four data words. Hardware executes
the ordinary two-cycle jump without writing registers, flags or peripherals.
This is not zero-cost on hardware, but makes no electrical changes. Host-side
builds emit no marker. UZE-only builds should omit configuration hints to save flash; speed hints
remain useful while loading.

Wire format (16-bit little-endian words):

```
0xC004, 0x4355, 0x4848, command | (value << 8), ~(command | (value << 8))
```

The last word is truncated to 16 bits. Configuration command assignments:

| Command | Meaning | Values |
|---|---|---|
| 1..7 | Video masks | Selected video bits |
| 0x80 | SPI RAM | 64 KiB bank count |
| 0x81 | ESP8266 UART | 0/1 |
| 0x83 | SD timing | 0=default, 1=slow, 2=normal, 3=fast |
| 0x84 | UzeLink UART | 0/1 |
| 0x85 | ESP8266 SoftAP and UART | 0/1 |

API option 0x82 remains reserved for the separate speed-marker format above.
The public helper maps display values 0/1 to their
corresponding wire bit; options must not be ORed together. Unknown commands, invalid bank counts, bad signatures,
and incorrect complements are ignored. Marker recognition shares the existing
loading-speed instruction dispatch; ordinary instruction and pixel loops do
not gain hint polling. The existing speed marker remains unchanged.

Validation: `python3 tools/test_hex_hint_helper.py` checks AVR encoding and
invalid arguments. `make -f tests/preferences/Makefile interrupt-test` exercises
both CPU decoders, reset behavior, SD timing and UART/SoftAP overrides, conflicts,
and UZE header precedence. `rom-loading-test` checks the existing ROM load paths.
