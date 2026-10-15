#!/usr/bin/env python3
"""SD SPI reinitialization after a CRC-enabled bootloader session."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_sd_timing import SDL

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include "cu_spisd.h"
void cu_vfat_reset(void){}
auint cu_vfat_read(auint addr){return addr&255U;}
void cu_vfat_write(auint addr,auint data){(void)addr;(void)data;}
static auint cycle;
static void byte(auint value){ cycle+=1024U; cu_spisd_recv(cycle-1024U); cu_spisd_send(value,cycle); }
static auint command(auint cmd,auint arg,auint crc){
 cu_spisd_cs_set(FALSE,cycle); byte(255U); cu_spisd_cs_set(TRUE,cycle);
 byte(0x40U|cmd); byte(arg>>24); byte((arg>>16)&255U); byte((arg>>8)&255U); byte(arg&255U); byte(crc); byte(255U);
 return cu_spisd_get_state()->data;
}
int main(void){
 cu_state_spisd_t* s; auint state,i;
 cu_spisd_reset(0); s=cu_spisd_get_state();
 for(i=0;i<10;++i) byte(255U);
 assert(command(0,0,0x95U)==1U && !s->crc);
 /* CMD59 enables CRC as used by the resident bootloader. */
 assert(command(59,1,1)==1U && s->crc);
 /* Cover initialized and initializing cards. These are stable state IDs in
 ** the saved peripheral state: IDLE=3, VERIFIED=4, IINIT=5, AVAIL=6. */
 for(state=3;state<=6;++state){
  s->state=state; s->cmd=0; s->crc=TRUE; s->pstat=0;
  assert((command(0,0,1)&8U)!=0U && s->crc); /* Reject invalid CMD0 CRC. */
  assert(command(0,0,0x95U)==1U);
  assert(!s->crc); /* A successful reset restores the default CRC-off option. */
  assert(command(8,0x1AAU,0x87U)==1U);
  assert(command(55,0,1)==1U); /* Petit FAT sends dummy CRC after reset. */
 }
 puts("SD reinitialization regression: PASS"); return 0;
}
'''


def main():
    cc = os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc')
    if not cc:
        raise SystemExit('No host C compiler')
    with tempfile.TemporaryDirectory(prefix='cuzebox-sd-reinit-') as temp:
        directory = Path(temp)
        (directory / 'SDL2').mkdir()
        (directory / 'SDL2/SDL.h').write_text(SDL)
        source = directory / 'test.c'
        source.write_text(HARNESS)
        executable = directory / 'test'
        subprocess.run([cc, '-std=gnu99', '-I', str(directory), '-I', str(ROOT),
                        str(ROOT / 'cu_spisd.c'), str(source), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    main()
