#!/usr/bin/env python3
"""Verify AVR encoding and compile-time rejection of invalid HEX hints."""
from pathlib import Path
import argparse
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--kernel', type=Path, help='Also check uzebox.h in this kernel directory')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
headers = [(root, 'extras/cuzebox_hex_hints.h')]
if args.kernel:
    headers.append((args.kernel, 'uzebox.h'))

# Literal wire bytes independently verify the public helper's encoding.
cases = [
    ('EMU_HINT_AREA_WEIGHTED', 1, '04c0554348480404fbfb'),
    ('EMU_HINT_AREA_WEIGHTED', 0, '04c0554348480400fbff'),
    ('EMU_HINT_INTERLACE', 1, '04c0554348480101fefe'),
    ('EMU_HINT_WEAVE', 1, '04c0554348480202fdfd'),
    ('EMU_HINT_SPI_RAM', 2, '04c05543484880027ffd'),
    ('EMU_HINT_SPI_RAM', 0, '04c05543484880007fff'),
    ('EMU_HINT_SPI_RAM', 128, '04c05543484880807f7f'),
    ('EMU_HINT_ESP8266', 1, '04c05543484881017efe'),
    ('EMU_HINT_ESP8266', 0, '04c05543484881007eff'),
    ('EMU_HINT_SPEED', 1, '04c0554358450100feff'),
    ('EMU_HINT_SPEED', 0, '04c0554358450000ffff'),
    ('EMU_HINT_SD_TIMING', 0, '04c05543484883007cff'),
    ('EMU_HINT_SD_TIMING', 1, '04c05543484883017cfe'),
    ('EMU_HINT_SD_TIMING', 2, '04c05543484883027cfd'),
    ('EMU_HINT_SD_TIMING', 3, '04c05543484883037cfc'),
    ('EMU_HINT_UZELINK', 1, '04c05543484884017bfe'),
    ('EMU_HINT_UZELINK', 0, '04c05543484884007bff'),
    ('EMU_HINT_ESP8266_AP', 1, '04c05543484885017afe'),
    ('EMU_HINT_ESP8266_AP', 0, '04c05543484885007aff'),
]
invalid = [('EMU_HINT_SPI_RAM', v) for v in (-1, 3, 256)]
invalid += [('EMU_HINT_SD_TIMING', v) for v in (-1, 4, 256)]
invalid += [(o, v) for o in ('EMU_HINT_AREA_WEIGHTED', 'EMU_HINT_INTERLACE',
                             'EMU_HINT_WEAVE', 'EMU_HINT_ESP8266', 'EMU_HINT_SPEED',
                             'EMU_HINT_UZELINK', 'EMU_HINT_ESP8266_AP') for v in (-1, 2)]
invalid += [('3', 1), ('0x82', 2), ('0x86', 0), ('255', 0)]

with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    src = p / 'helper.c'
    for include_dir, header in headers:
        prefix = '#include "' + header + '"\n'
        for optimization in ('-O0', '-Os'):
            command = ['avr-gcc', '-mmcu=atmega644', optimization, '-nostdlib',
                       '-Wl,-e,exercise', '-I', str(include_dir), str(src), '-o', str(p/'helper.elf')]
            body = ''.join(f'EmulatorHint({option},{value});' for option, value, _ in cases)
            src.write_text(prefix + 'void exercise(void){' + body + '}')
            subprocess.run(command, check=True)
            subprocess.run(['avr-objcopy', '-O', 'binary', '-j', '.text', str(p/'helper.elf'), str(p/'helper.bin')], check=True)
            code = (p/'helper.bin').read_bytes()
            expected = b''.join(bytes.fromhex(wire) for _, _, wire in cases)
            assert expected in code, (header, optimization, code.hex())
            if optimization == '-Os':
                assert code == expected + bytes.fromhex('0895'), code.hex()
            for option, value in invalid:
                call = f'EmulatorHint({option},{value})'
                src.write_text(prefix + 'void exercise(void){' + call + ';}')
                assert subprocess.run(command, capture_output=True).returncode != 0, (header, call)
            src.write_text(prefix + 'void exercise(void){volatile unsigned char value=1;EmulatorHint(EMU_HINT_SD_TIMING,value);}')
            assert subprocess.run(command, capture_output=True).returncode != 0, 'nonconstant argument accepted'
        print(f'PASS: {header}: exact AVR marker bytes at -O0/-Os, invalid arguments rejected')
