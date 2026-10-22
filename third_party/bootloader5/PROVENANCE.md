# Resident bootloader

Source snapshot from `C:/uzebox-master/demos/Bootloader5`, 2026-10-22.
The existing source copyright and GPL notices apply.

Build with AVR GCC and avr-libc:

```sh
make -C default all
```

`default/Bootloader5.hex` is version 5.0.13. The browser overlay embeds its
4 KiB boot section, retaining SD CRC fallback, saved-status restoration during
flash programming, and application-requested ROM loading. MegaDoom uses the
resident API to load its map records from its appended SD payload.

After rebuilding, copy `default/Bootloader5.hex` to the repository's root
`Bootloader.hex`, then run from the repository root:

```sh
python3 tools/bootloader_hex_to_c.py --input Bootloader.hex --output bootloader_builtin.h
```
