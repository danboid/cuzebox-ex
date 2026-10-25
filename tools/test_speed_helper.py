#!/usr/bin/env python3
"""Check the public AVR helper against the documented wire bytes."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)
 (p/'helper.c').write_text('#include "extras/cuzebox_hex_hints.h"\nvoid exercise(void){EmulatorHint(EMU_HINT_SPEED,1);EmulatorHint(EMU_HINT_SPEED,0);}\n')
 subprocess.run(['avr-gcc','-mmcu=atmega644','-Os','-nostdlib','-Wl,-e,exercise','-I',str(root),str(p/'helper.c'),'-o',str(p/'helper.elf')],check=True)
 subprocess.run(['avr-objcopy','-O','binary','-j','.text',str(p/'helper.elf'),str(p/'helper.bin')],check=True)
 expected=bytes.fromhex('04c0554358450100feff04c0554358450000ffff0895')
 assert (p/'helper.bin').read_bytes()==expected
 print('PASS: AVR-GCC helper emits two exact ten-byte markers followed by RET')
