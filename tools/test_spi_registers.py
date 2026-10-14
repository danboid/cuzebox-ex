#!/usr/bin/env python3
"""Exercise CUzeBox's actual SPI register helpers, without an SDL window."""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
TEST=r'''
#include <assert.h>
#include <string.h>
#include "cu_avr_spi.h"
static cu_state_cpu_t s, saved;
int main(void){
 const unsigned expected[2][4]={{32,128,512,1024},{16,64,256,512}};
 for(unsigned fast=0;fast<2;fast++)for(unsigned sel=0;sel<4;sel++){
  memset(&s,0,sizeof(s));s.cycle=100;
  s.iors[CU_IO_SPCR]=0x50|sel;s.iors[CU_IO_SPSR]=fast;
  assert(cu_avr_spi_start(&s,0xAF));
  assert(s.spi_end==100+expected[fast][sel]);
  unsigned deadline=s.spi_end;
  s.cycle=deadline-1;
  assert(!cu_avr_spi_start(&s,0xCC)); /* Even one cycle early collides. */
  assert(s.spi_tx==0xAF && s.spi_end==deadline);
  assert((s.iors[CU_IO_SPSR]&0xC0)==0x40);
  s.cycle=deadline;s.spi_rx=0x37;cu_avr_spi_complete(&s);
  assert(s.iors[CU_IO_SPDR]==0x37 && !s.spi_tran);
  assert((cu_avr_spi_status(&s)&0xC0)==0xC0);
  saved=s;s.spi_clear=0;s=saved; /* Snapshot must retain the clear latch. */
  cu_avr_spi_access(&s);
  assert((s.iors[CU_IO_SPSR]&0xC0)==0);
 }
 memset(&s,0,sizeof(s));s.iors[CU_IO_SPCR]=0x50;s.iors[CU_IO_SPSR]=1;
 cu_avr_spi_status(&s); /* Reading status BEFORE completion must not arm it. */
 assert(cu_avr_spi_start(&s,1));s.spi_rx=0xA5;cu_avr_spi_complete(&s);
 cu_avr_spi_access(&s);assert(s.iors[CU_IO_SPSR]&0x80);
 assert(cu_avr_spi_start(&s,2));assert(s.iors[CU_IO_SPSR]&0x80);
 assert(s.iors[CU_IO_SPDR]==0xA5); /* TX never replaces the receive buffer. */
 cu_avr_spi_status(&s);cu_avr_spi_access(&s);
 assert(!(s.iors[CU_IO_SPSR]&0x80));
 s.spi_rx=0x5A;cu_avr_spi_complete(&s);
 cu_avr_spi_status(&s);assert(cu_avr_spi_start(&s,3));
 assert(!(s.iors[CU_IO_SPSR]&0x80)); /* Status read followed by DATA WRITE. */
 s.spi_tran=FALSE;s.cycle=0xFFFFFFF8U;
 assert(cu_avr_spi_start(&s,4));assert(s.spi_end==8);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    d=Path(d)
    (d/'test.c').write_text(TEST)
    flags=subprocess.check_output(['sdl2-config','--cflags'],text=True).split()
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',*flags,'-I',str(ROOT),str(d/'test.c'),'-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)
print('PASS: SPI divider timing, early collisions, RX isolation, stale flags, read/write clearing, snapshot latch, cycle wrap')
