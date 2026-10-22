#include "cu_esp_rom.h"
#ifdef ENABLE_ESP_ROM
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define CAPACITY (4u*1024u*1024u)
#define PACKET 1024u
static uint8 *flash;
static char path[1024]="esp-flash.bin";
/* 0: configured profile, 1: incomplete, 2: supported image, 3: unsupported. */
static unsigned image_state,mode,loaded;
static unsigned reboot_pending;
static uint8 packet[PACKET+24];
static unsigned used,started,escaped,discard;
static uint32 address,blocks,sequence,block_size,registers[17];
static uint32 sys_offset,sys_length,sys_pos;
static unsigned identify(void);

static uint32 get32(const uint8 *p){return (uint32)p[0]|((uint32)p[1]<<8)|((uint32)p[2]<<16)|((uint32)p[3]<<24);}
static void put32(uint8 *p,uint32 v){for(unsigned i=0;i<4;i++){p[i]=(uint8)v;v>>=8;}}
static int load(void){
 if(loaded)return flash!=NULL;
 loaded=1;flash=malloc(CAPACITY);if(!flash)return 0;
 memset(flash,255,CAPACITY);
 FILE *f=fopen(path,"rb");if(!f){if(errno!=ENOENT)image_state=1;return 1;}
 uint8 h[16];image_state=1;
 if(fread(h,1,16,f)==16&&!memcmp(h,"CUEROM01",8)&&get32(h+8)==CAPACITY&&h[12]<=3&&
    fread(flash,1,CAPACITY,f)==CAPACITY&&fgetc(f)==EOF)image_state=h[12];
 fclose(f);if(image_state==2&&identify()!=2)image_state=1;return 1;
}
static int save(uint32 offset,uint32 length){
 uint8 h[16]={0};memcpy(h,"CUEROM01",8);put32(h+8,CAPACITY);h[12]=(uint8)image_state;
 FILE *f=fopen(path,"r+b");
 if(f){
  if(fseek(f,0,SEEK_END)||ftell(f)!=(long)(16+CAPACITY)){fclose(f);f=NULL;}
  else rewind(f);
 }
 if(!f){f=fopen(path,"wb");offset=0;length=CAPACITY;}
 if(!f)return 0;
 int ok=fwrite(h,1,16,f)==16&&fseek(f,16+(long)offset,SEEK_SET)==0&&fwrite(flash+offset,1,length,f)==length;
 if(fclose(f))ok=0;
 return ok;
}
void cu_esp_rom_set_path(const char *config){
 free(flash);flash=NULL;loaded=image_state=mode=0;sys_length=reboot_pending=0;
 if(config&&*config){
  if(snprintf(path,sizeof(path),"%s.esp-flash.bin",config)>=(int)sizeof(path))path[0]=0;
 }else strcpy(path,"esp-flash.bin");
}
/* ESP8266 image checksum covers segment data, with a 16-byte aligned trailer. */
static int image_ok(uint32 offset){
 if(offset>CAPACITY-8||flash[offset]!=0xe9||!flash[offset+1]||flash[offset+1]>16)return 0;
 unsigned segments=flash[offset+1];uint32 pos=offset+8;uint8 checksum=0xef;
 for(unsigned i=0;i<segments;i++){
  if(pos>CAPACITY-8)return 0;
  uint32 n=get32(flash+pos+4);pos+=8;if(n>CAPACITY-pos)return 0;
  while(n--)checksum^=flash[pos++];
 }
 pos|=15u;return pos<CAPACITY&&flash[pos]==checksum;
}
static unsigned identify(void){
 if(!image_ok(0)||!image_ok(0x10000))return 1;
 static const char marker[]="+UZFW:4,1";
 for(uint32 i=0x10000;i<0xe0000-sizeof(marker);i++)
  if(!memcmp(flash+i,marker,sizeof(marker)-1))return 2;
 return 3;
}
int cu_esp_rom_reset(int gpio0_low){
 sys_length=reboot_pending=0;
 used=started=escaped=discard=0;blocks=sequence=0;memset(registers,0,sizeof(registers));
 if(!load()){image_state=1;mode=0;return 1;}
 mode=gpio0_low!=0;return mode?4:(int)image_state;
}
int cu_esp_rom_active(void){return mode!=0;}
int cu_esp_rom_blocked(void){return mode||image_state==1||image_state==3;}
int cu_esp_rom_reboot_pending(void){return reboot_pending!=0;}
int cu_esp_rom_partition(const char *name,uint32 *offset,uint32 *size){
 if(!load()||strlen(name)>16)return 0;
 for(uint32 table=0xe0000;table<=0xf0000;table+=0x10000){
  for(uint32 pos=table;pos<table+3072;pos+=32){
   const uint8 *p=flash+pos;
   if(p[0]!=0xaa||p[1]!=0x50)break;
   if(!strncmp((const char*)p+12,name,16)){
    *offset=get32(p+4);*size=get32(p+8);
    return *offset<CAPACITY&&*size<=CAPACITY-*offset;
   }
  }
 }
 if(!image_state){ /* Built-in firmware profile has the default PKI layout. */
  static const char *names[]={"client_cert","client_key","client_ca"};
  for(unsigned i=0;i<3;i++)if(!strcmp(name,names[i])){*offset=0xf8000+i*8192;*size=8192;return 1;}
 }
 return 0;
}
const uint8 *cu_esp_rom_read(uint32 offset,uint32 size){
 return load()&&offset<CAPACITY&&size<=CAPACITY-offset?flash+offset:NULL;
}
int cu_esp_rom_erase(uint32 offset,uint32 size){
 if(!load()||offset%4096||size%4096||offset>=CAPACITY||size>CAPACITY-offset)return 0;
 memset(flash+offset,255,size);return save(offset,size);
}
int cu_esp_rom_write_begin(uint32 offset,uint32 size){
 if(!load()||!size||offset>=CAPACITY||size>CAPACITY-offset)return 0;
 sys_offset=offset;sys_length=size;sys_pos=0;return 1;
}
int cu_esp_rom_write_byte(uint8 byte){
 if(!sys_length)return 0;
 flash[sys_offset+sys_pos++]&=byte;
 if(sys_pos==sys_length){
  int ok=save(sys_offset,sys_length);sys_length=0;
  const char *text=ok?"\r\nOK\r\n":"\r\nERROR\r\n";
  cu_esp_rom_output((const uint8*)text,(unsigned)strlen(text));
 }
 return 1;
}
const uint8 *cu_esp_rom_pki(const char *name,unsigned slot,unsigned *size){
 uint32 offset,length;if(!cu_esp_rom_partition(name,&offset,&length)||length<8)return NULL;
 const uint8 *p=flash+offset;
 if(p[0]!=0xf1||p[1]!=0xf1)return NULL;
 unsigned count=p[2]|p[3]<<8;uint32 total=get32(p+4);
 if(total>length-8||count>8)return NULL;
 uint32 pos=8,end=8+total;
 for(unsigned i=0;i<count;i++){
  if(end-pos<4)return NULL;
  unsigned n=p[pos+2]|p[pos+3]<<8;unsigned record=p[pos+1];pos+=4;
  uint32 padded=(n+3u)&~3u;if(!n||padded>end-pos)return NULL;
  if(record==slot){*size=n;return p+pos;}
  pos+=padded;
 }
 return NULL;
}
static void reply(uint8 op,uint32 value,uint8 error){
 uint8 p[10]={1,op,2,0,0,0,0,0,0,0},out[22];unsigned n=0;
 put32(p+4,value);p[8]=error!=0;p[9]=error;out[n++]=0xc0;
 for(unsigned i=0;i<10;i++){
  if(p[i]==0xc0||p[i]==0xdb){out[n++]=0xdb;out[n++]=p[i]==0xc0?0xdc:0xdd;}
  else out[n++]=p[i];
 }
 out[n++]=0xc0;cu_esp_rom_output(out,n);
}
static void command(void){
 uint8 op=packet[1],error=0;uint32 value=0;unsigned n=used-8;uint8 *p=packet+8;
 if(packet[0]||n!=(unsigned)(packet[2]|packet[3]<<8)){reply(op,0,5);return;}
 switch(op){
 case 8:
  if(n!=36||memcmp(p,"\x07\x07\x12\x20",4)){error=5;break;}
  for(unsigned i=4;i<36;i++)if(p[i]!=0x55)error=5;
  break;
 case 2:{
  if(n!=16){error=1;break;}
  uint32 count=get32(p+4),size=get32(p+8),offset=get32(p+12);
  if(!count&&!get32(p))break; /* Attach flash for RDID. */
  if(!count||size!=PACKET||offset%4096||offset>=CAPACITY||count>(CAPACITY-offset)/size){error=1;break;}
  address=offset;blocks=count;block_size=size;sequence=0;
  uint32 length=(count*size+4095u)&~4095u;
  if(length>CAPACITY-offset){blocks=0;error=1;break;}
  image_state=1;
  /* Mark incomplete before erasing so a host interruption cannot fake boot. */
  if(!save(0,0)){blocks=0;error=8;break;}
  memset(flash+offset,255,length);
  if(!save(offset,length)){blocks=0;error=8;}
  break;
 }
 case 3:{
  if(n!=16+PACKET||get32(p)!=PACKET||get32(p+8)||get32(p+12)){error=1;break;}
  uint32 seq=get32(p+4);uint8 sum=0xef;
  for(unsigned i=16;i<n;i++)sum^=p[i];
  if(sum!=packet[4]){error=7;break;}
  if(!blocks||seq>=blocks||seq>sequence){error=1;break;}
  uint32 offset=address+seq*block_size;
  if(seq<sequence){
   if(memcmp(flash+offset,p+16,PACKET))error=8;
   else if(sequence==blocks){image_state=identify();if(!save(0,0)){image_state=1;error=8;}}
   break;
  }
  memcpy(flash+offset,p+16,PACKET);
  if(!save(offset,PACKET)){error=8;break;}
  sequence++;
  if(sequence==blocks){image_state=identify();if(!save(0,0)){image_state=1;error=8;}}
  break;
 }
 case 4:
  /* ROM-only clients should reset the module after programming. */
  if(n!=4||sequence!=blocks||!blocks)error=1;
  else if(get32(p)==0)reboot_pending=1;
  else error=1;
  break;
 case 9:
 case 10:{
  if(n!=(op==9?16u:4u)){error=1;break;}
  uint32 reg=get32(p);
  if(op==10&&reg==0x40001000){value=0xfff0c101;break;}
  if(reg<0x60000200||reg>0x60000240||(reg&3)){error=1;break;}
  unsigned index=(reg-0x60000200)/4;
  if(op==9){uint32 mask=get32(p+8);registers[index]=(registers[index]&~mask)|(get32(p+4)&mask);
   if(index==0&&(registers[0]&(1u<<18))){
    if((registers[9]&255)==0x9f)registers[16]=0x1640ef; /* Winbond, 4 MiB */
    registers[0]&=~(1u<<18);
   }
  }else value=registers[index];
  break;
 }
 default:error=1;break;
 }
 reply(op,value,error);
}
void cu_esp_rom_byte(uint8 b){
 if(!mode)return;
 if(b==0xc0){
  if(started&&!discard&&!escaped&&used>=8)command();
  started=1;used=escaped=discard=0;return;
 }
 if(!started||discard)return;
 if(escaped){
  if(b==0xdc)b=0xc0;else if(b==0xdd)b=0xdb;else{discard=1;return;}
  escaped=0;
 }else if(b==0xdb){escaped=1;return;}
 if(used==sizeof(packet)){discard=1;return;}
 packet[used++]=b;
}
#endif
