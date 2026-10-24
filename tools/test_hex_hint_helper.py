#!/usr/bin/env python3
"""Verify AVR encoding and compile-time rejection of invalid HEX hints."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    src = p / "helper.c"
    src.write_text('#include "extras/cuzebox_hex_hints.h"\nvoid exercise(void){EmulatorHint(EMU_HINT_AREA_WEIGHTED,1);EmulatorHint(EMU_HINT_SPI_RAM,2);EmulatorHint(EMU_HINT_SPI_RAM,0);EmulatorHint(EMU_HINT_AREA_WEIGHTED,0);EmulatorHint(EMU_HINT_INTERLACE,1);EmulatorHint(EMU_HINT_WEAVE,1);}')
    command = ['avr-gcc', '-mmcu=atmega644', '-Os', '-nostdlib', '-Wl,-e,exercise', '-I', str(root), str(src), '-o', str(p/'helper.elf')]
    subprocess.run(command, check=True)
    subprocess.run(['avr-objcopy', '-O', 'binary', '-j', '.text', str(p/'helper.elf'), str(p/'helper.bin')], check=True)
    assert (p/'helper.bin').read_bytes() == bytes.fromhex('04c0554348480404fbfb04c05543484880027ffd04c05543484880007fff04c0554348480400fbff04c0554348480101fefe04c0554348480202fdfd0895')
    for call in ('EmulatorHint(EMU_HINT_SPI_RAM,3)', 'EmulatorHint(EMU_HINT_SPI_RAM,256)', 'EmulatorHint(EMU_HINT_AREA_WEIGHTED,2)', 'EmulatorHint(3,1)', 'EmulatorHint(255,0)', 'EmulatorHint(EMU_HINT_SPI_RAM,-1)'):
        src.write_text('#include "extras/cuzebox_hex_hints.h"\nvoid exercise(void){' + call + ';}')
        assert subprocess.run(command, capture_output=True).returncode != 0, call
print('PASS: exact AVR HEX marker bytes, invalid arguments rejected')
