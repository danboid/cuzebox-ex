#include "cu_esp_rom.h"
#include "esp_rom.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8 response[4096];
static unsigned read_pos,write_pos;
static unsigned corrupt,wire_count;
void cu_esp_rom_output(const uint8 *data,unsigned length){
 assert(write_pos+length<=sizeof(response));memcpy(response+write_pos,data,length);write_pos+=length;
}
void EspUartFlushRx(void){read_pos=write_pos=0;}
void EspUartPut(u8 byte){if(corrupt&&wire_count++==5)byte^=1;cu_esp_rom_byte(byte);}
s16 EspUartGet(void){return read_pos<write_pos?response[read_pos++]:-1;}
void WaitVsync(int n){(void)n;}
int main(int argc,char **argv){
 assert(argc==3);cu_esp_rom_set_path(argv[1]);assert(cu_esp_rom_reset(0)==0);
 assert(cu_esp_rom_reset(1)==4);assert(EspRomSync(115200));
 u32 id;assert(EspRomFlashId(&id)&&id==0x1640ef);
 assert(!EspRomChangeBaud(115200,230400));
 u8 block[1024];memset(block,0xc0,sizeof(block));block[5]=0xdb;
 assert(!EspFlashBegin(4*1024*1024-4096,8192));
 assert(EspFlashBegin(0,8192));assert(!EspFlashBlock(block,1024,1));
 corrupt=1;wire_count=0;assert(!EspFlashBlock(block,1024,0));assert(EspRomLastError()==7);corrupt=0;
 assert(EspFlashBlock(block,1024,0));assert(EspFlashBlock(block,1024,0));
 block[0]^=1;assert(!EspFlashBlock(block,1024,0));
 assert(cu_esp_rom_reset(0)==1);assert(cu_esp_rom_blocked());
 cu_esp_rom_set_path(argv[1]);assert(cu_esp_rom_reset(0)==1);
 assert(cu_esp_rom_reset(1)==4);assert(EspRomSync(9600));
 /* A malformed oversized packet cannot poison the next SYNC. */
 cu_esp_rom_byte(0xc0);for(unsigned i=0;i<3000;i++)cu_esp_rom_byte(0x55);cu_esp_rom_byte(0xc0);
 assert(EspRomSync(115200));
 FILE *f=fopen(argv[2],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long size=ftell(f);rewind(f);
 assert(size>0&&size<=4*1024*1024&&size%1024==0);
 assert(EspFlashBegin(0,(u32)size));
 for(unsigned i=0;i<(unsigned)size/1024;i++){
  assert(fread(block,1,1024,f)==1024);assert(EspFlashBlock(block,1024,(u16)i));
 }
 fclose(f);assert(cu_esp_rom_reset(0)==2);assert(!cu_esp_rom_blocked());
 cu_esp_rom_set_path(argv[1]);assert(cu_esp_rom_reset(0)==2);
 assert(cu_esp_rom_reset(1)==4);assert(EspRomSync(115200));assert(EspRomFlashId(&id));
 puts("PASS: production UNTool ROM client, RDID, bounds, retries, interrupted flash, persistence, complete firmware recognition");
}
