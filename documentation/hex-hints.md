# .hex hints

HEX programs can request display flags and SPI RAM capacity without a UZE
header. UZE programs use their header; the emulator ignores these markers
when a UZE is loaded. No ROM scanning or high-resolution autodetection is used.

The Uzebox kernel exposes these helpers through `uzebox.h` / `emulator.h`.
For other AVR projects, include `extras/cuzebox_hex_hints.h`.

```c
EmulatorHint(EMU_HINT_AREA_WEIGHTED, 1);
EmulatorHint(EMU_HINT_INTERLACE, 1);
EmulatorHint(EMU_HINT_WEAVE, 0); /* field presentation */
EmulatorHint(EMU_HINT_SPI_RAM, 2); /* 128 KiB, before SPI RAM initialization */
```

`EmulatorHint(option, value)` changes one option. Display options accept 0
(off) or 1 (on). Arguments must be compile-time constants; unknown options
and invalid values are rejected at compile time. Each call occupies ten bytes.

`EmulatorHint(EMU_HINT_SPI_RAM, banks)` selects 0, 1, 2, 4, 8, 16, 32, 64, or 128 banks of
64 KiB. Zero disables SPI RAM. Set it before initializing or relying on the
RAM: the request resets its transaction state and updates its capacity before
the following AVR instruction executes. RAM contents after resizing must not
be relied on. This configures emulation; it does not detect or add physical RAM.

Explicit GUI/config overrides win. `.UZE HINT` selections also follow HEX
hints when running HEX. Hints do not change saved preferences. CPU/watchdog
reset clears them; HEX starts with nearest sampling, no interlace, and the
ordinary 128 KiB SPI default unless overridden. Issue hints again at startup.

Each helper emits ten bytes: an RJMP over four data words. Hardware executes
the ordinary two-cycle jump without writing registers, flags or peripherals.
This is not zero-cost on hardware, but makes no electrical changes. Host-side
builds emit no marker. UZE-only builds should omit these calls to save flash.

Wire format (16-bit little-endian words):

```
0xC004, 0x4355, 0x4848, command | (value << 8), ~(command | (value << 8))
```

The last word is truncated to 16 bits. Commands 1..7 are video masks; command
0x80 selects SPI banks. The public helper maps display values 0/1 to their
corresponding wire bit; options must not be ORed together. Unknown commands, invalid bank counts, bad signatures,
and incorrect complements are ignored. Marker recognition shares the existing
loading-speed instruction dispatch; ordinary instruction and pixel loops do
not gain hint polling. The existing speed marker remains unchanged.
