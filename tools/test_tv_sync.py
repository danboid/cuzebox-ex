#!/usr/bin/env python3
"""Check the production TV PLL with synthetic sync waveforms (no SDL needed)."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TEST = r'''
#include <assert.h>
#include <stdlib.h>
#include "tv_sync.h"
int main(void) {
 tv_sync_t s;
 uint32_t t = 0xfffff000U;
 int x;
 tv_sync_reset(&s);
 assert(tv_sync_edge(&s,t,100) == 0);
 /* Normal and vertical half-line timing, including timestamp wrap. */
 for (int field=0;field<8;field++) {
  for (int row=0;row<253;row++) assert(tv_sync_edge(&s,t+=1820,100)==0);
  for (int row=0;row<19;row++) assert(tv_sync_edge(&s,t+=910,100)==0);
 }
 /* A delayed sync moves the sampling origin earlier, then recovers. */
 x=tv_sync_edge(&s,t+=1900,100); assert(x< -60 && x> -80);
 for(int row=0;row<80;row++) x=tv_sync_edge(&s,t+=1820,100);
 assert(abs(x)<3);
 /* Track a sustained two-clock line period error without steady skew. */
 for(int row=0;row<500;row++) x=tv_sync_edge(&s,t+=1822,100);
 assert(abs(x)<2);
 /* Reacquire after a long loss, including restoring an earlier cycle. */
 assert(tv_sync_edge(&s,t+=1000000,100)==0);
 assert(tv_sync_edge(&s,t-=100000,100)==0);
 /* Rendering window bounds, even with adversarial sync intervals. */
 for(int i=0;i<100000;i++) {
  x=tv_sync_edge(&s,t+=(unsigned)(455+rand()%3642),100);
  assert(x>=-240 && x<=240);
  assert(291+x-1>=0 && 291+x+1456<2032);
  assert(299+x>=0 && 299+x+1439<2032);
 }
 tv_sync_reset(&s);
 assert(tv_sync_edge(&s,1234,100)==0);
 /* Faster response reduces initial displacement and integrated error. */
 for(unsigned speed=25; speed<=800; speed++) {
  tv_sync_reset(&s);t=0;tv_sync_edge(&s,t,speed);
  for(int i=0;i<100;i++) assert(tv_sync_edge(&s,t+=1820,speed)==0);
  x=tv_sync_edge(&s,t+=1900,speed);
  assert(x<=0 && x>=-78);
  if(speed>=400) assert(abs(x)<=40);
  if(speed==800) assert(x==0);
  for(int i=0;i<2000;i++) x=tv_sync_edge(&s,t+=1822,speed);
  assert(abs(x)<2);
 }
 assert(tv_sync_recovery_clamp(0)==25);
 assert(tv_sync_recovery_clamp(99999)==800);
 for(int offset=-240; offset<=240; offset++) {
  assert(tv_sync_scale(offset,0)==0);
  assert(tv_sync_scale(offset,100)==offset);
  assert(tv_sync_scale(offset,999)==offset);
  assert(tv_sync_scale(offset,50)==offset/2);
 }
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    src = tmp / 'test.c'
    exe = tmp / ('test.exe' if os.name == 'nt' else 'test')
    src.write_text(TEST)
    subprocess.run([os.environ.get('CC', 'gcc'), '-std=c99', '-Wall', '-Wextra',
                    '-Werror', '-I', str(ROOT), str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: stable sync, half-lines, phase recovery, frequency tracking, '
      'clock wrap, lost sync, reset, renderer bounds')
