# ESP8266 ROM loader and virtual flash

ESP-enabled builds model the ROM download protocol used by UNTool. The model
lives in `cu_esp_rom.c`, separately from the AT and UzeNet firmware adapters.
It does not execute Xtensa instructions or arbitrary uploaded firmware.

## Using UNTool

Select the **ESP MODULE** serial route and enable ESP emulation. Run UNTool
with its appended firmware payload available on the virtual SD card. Its
normal install workflow can enter the loader, identify flash, upload firmware,
reset the module and query the installed firmware identity.

The board connections are PA7 to GPIO0 and PD3 to RESET. Driving reset low,
then releasing it with GPIO0 low, enters the ROM loader. Input pins model the
board's external pull-ups, including UNTool's open-drain reset sequence.

The simulated device has 4 MiB flash. A complete supported UzeNet v4 image
activates the UzeNet firmware adapter at 9600 baud; `AT+UZFW?` returns
`+UZFW:4,1`. An incomplete image cannot boot the AT adapter, but the ROM loader
remains available for recovery. Valid unsupported firmware is retained in flash
but does not acquire simulated behavior.

## Persistence and credentials

Virtual flash is stored in `esp-flash.bin` in the working directory. With an
explicit `--config player.cfg`, the backing file is
`player.cfg.esp-flash.bin`. Use separate configuration paths for separate
devices; do not share a writable backing file between running instances.

The file contains a 16-byte emulator header followed by the raw 4 MiB flash.
Acknowledged upload blocks are written to it. Removing it while the emulator
is closed returns the module to the configured built-in firmware profile.
These files are ignored by Git because they can contain private credentials.

`AT+SYSFLASH` supports the named `client_ca`, `client_cert` and `client_key`
partitions, including streamed writes larger than the AT command buffer. An
uploaded image supplies its partition table; a fresh built-in profile uses the
default UzeNet PKI layout. Native OpenSSL builds can use the stored PEM
credentials for TLS. Explicit certificate/key file selections take precedence.

## Protocol and limits

The model follows Espressif's [ROM serial protocol](https://docs.espressif.com/projects/esptool/en/latest/esp8266/advanced-topics/serial-protocol.html):

- SLIP framing, synchronization and ROM responses.
- Register reads/writes needed for chip identification and SPI flash RDID.
- Flash begin, 1024-byte data packets and flash end/reset.
- Packet length, checksum, address and sequence validation; identical retries.
- Persistent incomplete-image state when programming is interrupted.

Image recognition checks ESP image checksums and the supported firmware marker;
it is compatibility detection, not signature verification. RAM stubs and other
unimplemented commands return errors. Baud acquisition, erase behavior and
flash operation delays are approximations, rather than cycle-accurate ESP8266
hardware. AVR UART transfer timing still applies.

Existing host-side Wi-Fi and network configuration remains separate: uploaded
NVS data is not interpreted. Flashing does not reproduce a hardware factory
reset of those host settings. Savestates and input movies do not snapshot the
virtual flash or its in-flight protocol state; do not use them to rewind a
flash or provisioning transaction. Radio, electrical faults and arbitrary
firmware execution still require real hardware testing.

## Validation

From a Linux shell, with UNTool sources and its factory image available:

```sh
tools/test_esp_rom.sh /path/to/UNTool /path/to/UZENET4.BIN
python3 tools/test_esp_rom_avr.py ./cuzebox /path/to/UNTool
tools/test_esp_pki.sh
```

The first test uses UNTool's actual ROM uploader, including a full factory
image and interrupted transfers. The second runs that uploader through AVR
GPIO/UART instructions. PKI checks exercise named-partition AT commands,
persistent writes, TLS credential loading and missing-key rejection. The host
protocol and PKI checks use address/undefined-behavior sanitizers.
