/* Logical-update tests. Hooks run on the emulation thread before an instruction.
 * GPL-3.0-or-later, like the emulator. */
#include "logical_test.h"
#include "cu_avr.h"
#include "cu_ctr.h"
#include "cu_spir.h"
#include "cu_ufile.h"
#include "cu_vfat.h"
#include "capture.h"
#include "savestate.h"
#include "mainui.h"
#include "filesys.h"
#include <errno.h>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif
#include <stdlib.h>
#include <string.h>

#define LT_INPUTS 4096U
#define LT_RANGES 16U
#define LT_BYTES 1024U
#define LT_RECORDS 4096U
#define LT_CHECKPOINTS 4U
typedef struct { unsigned region, address, length; } lt_range;
typedef struct { unsigned mask, hits; } lt_input;
typedef struct {
 unsigned pc, cycle, row, beam, update;
 uint8 bytes[LT_BYTES];
} lt_record;
typedef struct {
 unsigned input_pc, observe_pc, every, timeout, player;
 unsigned range_count, bytes, input_count, cursor, remaining, updates, count;
 unsigned hits, idle, target, condition, cond_region, cond_address, cond_mask, cond_value;
 unsigned mask;
 boole configured, active, resume_skip;
 char const *status;
 lt_range ranges[LT_RANGES];
 lt_input inputs[LT_INPUTS];
} lt_state;
static lt_state state;
static lt_record *records;
static uint8 *sd_image;
static unsigned sd_size;
static boole sd_read(auint pos,auint *value)
{ *value=pos<sd_size?sd_image[pos]:255; return TRUE; }
static boole sd_write(auint pos,auint value)
{ if(pos<sd_size) sd_image[pos]=(uint8)value; return TRUE; }
static void sd_install(uint8 *data,unsigned size)
{
 free(sd_image); sd_image=data; sd_size=size;
 cu_vfat_set_io_hooks(size?sd_read:NULL,size?sd_write:NULL);
}
typedef struct { savestate_blob_t machine; lt_state state; lt_record *records; uint8 *sd; unsigned sd_size; } lt_checkpoint;
static lt_checkpoint checkpoints[LT_CHECKPOINTS];
static boole movie_active(void) {
#ifdef ENABLE_ICAP
 return capture_is_active();
#else
 return FALSE;
#endif
}
void logical_test_reset(void)
{
 logical_test_stop();
 if(sd_image) sd_install(NULL,0);
 memset(&state,0,sizeof(state)); free(records); records=NULL;
 for(unsigned i=0;i<LT_CHECKPOINTS;i++) {
  savestate_free_blob(&checkpoints[i].machine); free(checkpoints[i].records);
  free(checkpoints[i].sd);
  memset(&checkpoints[i],0,sizeof(checkpoints[i]));
 }
}


static uint8 *memory(unsigned region, unsigned address, unsigned length)
{
 if (!region) {
  /* Raw SRAM indices, matching READ_MEM SRAM (AVR address & 0xfff). */
  if (address > 0x1000 || length > 0x1000-address) return NULL;
  return cu_avr_get_state()->sram + (address&0xfff);
 }
 cu_state_spir_t *spir = cu_spir_get_state();
 if (!spir->ram || address > spir->size || length > spir->size-address) return NULL;
 return spir->ram+address;
}
static boole copy_range(uint8 *out, lt_range r)
{
 /* SRAM's internal array wraps data address 0x1000, so read bytewise. */
 if (!memory(r.region,r.address,r.length)) return FALSE;
 for (unsigned i=0;i<r.length;i++) out[i]=*memory(r.region,r.address+i,1);
 return TRUE;
}
void logical_test_stop(void)
{
 if (state.active) cu_ctr_setslot_packet_override(state.player,0,FALSE,0,16);
 state.active=FALSE;
 cu_avr_set_execution_hook(NULL);
}
boole logical_test_active(void) { return state.active || sd_image!=NULL; }
static boole finish(char const *status)
{
 logical_test_stop(); state.resume_skip=TRUE; state.status=status; return TRUE;
}
static boole instruction(auint pc)
{
 if(pc>=32768) { logical_test_stop(); state.configured=FALSE; state.status="machine state changed"; return FALSE; }
 if (state.resume_skip) { state.resume_skip=FALSE; return FALSE; }
 if (state.idle >= state.timeout) return finish("timeout");
 ++state.idle;
 if (pc == state.input_pc) {
  boole eligible=TRUE;
  if (state.condition) {
   uint8 *p=memory(state.cond_region,state.cond_address,1);
   if (!p) return finish("invalid memory range");
   eligible=((*p & state.cond_mask)==state.cond_value);
  }
  if (eligible && ++state.hits == state.every) {
   state.hits=0; state.idle=0;
   if (!state.remaining) {
    if (state.cursor==state.input_count) return finish("inputs complete");
    state.mask=state.inputs[state.cursor].mask;
    state.remaining=state.inputs[state.cursor++].hits;
   }
   --state.remaining; ++state.updates;
   cu_ctr_setslot_packet_override(state.player,0,TRUE,state.mask,16);
  }
 }
 if (pc == state.observe_pc) {
  if (state.count==LT_RECORDS) return finish("buffer full");
  lt_record *r=&records[state.count]; unsigned offset=0;
  r->pc=pc; r->cycle=cu_avr_getcycle(); r->row=cu_avr_get_video_beam_pulse();
  r->beam=cu_avr_get_video_beam_cycle(); r->update=state.updates;
  for (unsigned i=0;i<state.range_count;i++) {
   if (!copy_range(r->bytes+offset,state.ranges[i])) return finish("invalid memory range");
   offset+=state.ranges[i].length;
  }
  ++state.count;
  if (state.target && state.count>=state.target) return finish("target reached");
 }
 return FALSE;
}
static char *token(char **s)
{
 char *p=*s; if (!p) return NULL;
 p+=strspn(p," \t\r\n"); if (!*p) { *s=p; return NULL; }
 char *end=p+strcspn(p," \t\r\n");
 if (*end) *end++=0;
 *s=end; return p;
}
static boole number(char *s,unsigned *out)
{
 char *end; unsigned long n;
 if (!s || !*s || *s=='-') return FALSE;
 errno=0; n=strtoul(s,&end,0);
 if (errno || *end || n>0xffffffffUL) return FALSE;
 *out=(unsigned)n; return TRUE;
}
static boole pc_value(char *s,unsigned *out)
{
 auint kind,addr;
 if (number(s,out)) return *out<32768;
 if (!s || !mainui_debug_find_symbol(s,&kind,&addr) || kind!=MAINUI_DBG_SYMBOL_PROG) return FALSE;
 *out=addr; return addr<32768;
}
static boole region(char *s,unsigned *out)
{
 if (s && !strcmp(s,"SRAM")) { *out=0; return TRUE; }
 if (s && (!strcmp(s,"SPI") || !strcmp(s,"SPIRAM"))) { *out=1; return TRUE; }
 return FALSE;
}
static void write_record(FILE *f,unsigned i)
{
 lt_record *r=&records[i];
 fprintf(f,"{\"index\":%u,\"update\":%u,\"pc\":%u,\"cycle\":%u,\"row\":%u,\"beam_cycle\":%u,\"hex\":\"",
         i,r->update,r->pc,r->cycle,r->row,r->beam);
 for (unsigned j=0;j<state.bytes;j++) fprintf(f,"%02x",r->bytes[j]);
 fputs("\"}\n",f);
}
#include "logical_checkpoint.inc"
void logical_test_command(char *command,char *reply,size_t capacity)
{
 char *op=token(&command), *error="Invalid LOGICAL command or arguments";
 unsigned a,b,c,d,e; boole ok=FALSE;
 mainui_netplay_runtime_t netplay; mainui_get_netplay_runtime(&netplay);
 if (op && netplay.active && (!strcmp(op,"START") || !strcmp(op,"CONTINUE") || !strcmp(op,"CHECKPOINT"))) { snprintf(reply,capacity,"{\"ok\":0,\"error\":\"Stop netplay before logical tests\"}"); return; }
 if (op && !strcmp(op,"CHECKPOINT") && state.active && mainui_debug_is_paused()) logical_test_stop();
 if (!op || !strcmp(op,"STATUS")) {
  snprintf(reply,capacity,"{\"ok\":1,\"active\":%u,\"status\":\"%s\",\"updates\":%u,\"records\":%u,\"bytes\":%u,\"cursor\":%u}",
           state.active,state.status?state.status:"idle",state.updates,state.count,state.bytes,state.cursor); return;
 }
 if (!strcmp(op,"STOP")) { logical_test_stop(); state.status="stopped"; ok=TRUE; }
 else if (!strcmp(op,"READ")) {
  if (number(token(&command),&a) && a<state.count && capacity>2*state.bytes+256) {
   lt_record *r=&records[a];
   size_t n=(size_t)snprintf(reply,capacity,"{\"ok\":1,\"index\":%u,\"update\":%u,\"pc\":%u,\"cycle\":%u,\"row\":%u,\"beam_cycle\":%u,\"hex\":\"",a,r->update,r->pc,r->cycle,r->row,r->beam);
   for (unsigned j=0;j<state.bytes;j++) n+=(size_t)snprintf(reply+n,capacity-n,"%02x",r->bytes[j]);
   snprintf(reply+n,capacity-n,"\"}"); return;
  }
 } else if (!strcmp(op,"SD")) {
  char *action=token(&command), *path=command;
  if(path) path+=strspn(path," \t");
  if(action && path && *path && !state.active && mainui_debug_is_paused() && !movie_active() && !netplay.active) {
   if(!strcmp(action,"LOAD")) {
    FILE *f=fopen(path,"rb");
    if(f) {
     if(!fseek(f,0,SEEK_END)) {
      long size=ftell(f);
      if(size>0 && size<=8*1024*1024 && !(size%512) && !fseek(f,0,SEEK_SET)) {
       uint8 *data=malloc((size_t)size);
       if(data && fread(data,1,(size_t)size,f)==(size_t)size) { sd_install(data,(unsigned)size); ok=TRUE; }
       else free(data);
      }
     }
     fclose(f);
    }
   } else if(!strcmp(action,"SAVE") && sd_image) {
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_BINARY,0666);
    FILE *f=fd<0?NULL:fdopen(fd,"wb"); if(fd>=0 && !f) close(fd);
    if(f) { ok=fwrite(sd_image,1,sd_size,f)==sd_size; if(fclose(f)) ok=FALSE; }
   }
  }
 } else if (!strcmp(op,"EXPORT")) {
  char *path=command; if(path) path+=strspn(path," \t");
  if (path && *path) {
   /* Exclusive create prevents accidental replacement of a ROM or movie. */
   int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_BINARY,0666);
   FILE *f=fd<0?NULL:fdopen(fd,"wb");
   if(fd>=0 && !f) close(fd);
   if(f) {
    fprintf(f,"{\"format\":\"CUZEBOX_LOGICAL_1\",\"behavior_id\":%u,\"flash_crc\":%u,\"input_pc\":%u,\"observe_pc\":%u,\"every\":%u,\"timeout_instructions\":%u,\"status\":\"%s\",\"records\":%u,\"ranges\":[",CAPTURE_BEHAVIOR_ID,(unsigned)cu_ufile_crc32(cu_avr_get_state()->crom,65536),state.input_pc,state.observe_pc,state.every,state.timeout,state.status?state.status:"idle",state.count);
    for(unsigned i=0;i<state.range_count;i++) fprintf(f,"%s{\"region\":\"%s\",\"address\":%u,\"length\":%u}",i?",":"",state.ranges[i].region?"SPI":"SRAM",state.ranges[i].address,state.ranges[i].length);
    fputs("]}\n",f); for(unsigned i=0;i<state.count;i++) write_record(f,i);
    ok=!ferror(f); if(fclose(f)) ok=FALSE;
   }
  }
 } else if (state.active) error="STOP the logical test before changing configuration or checkpoints";
 else if (!strcmp(op,"CLEAR")) {
  logical_test_reset();
  ok=TRUE;
 } else if (state.count && (!strcmp(op,"CONFIG") || !strcmp(op,"RANGE") || !strcmp(op,"CONDITION") || !strcmp(op,"INPUT"))) {
  error="CLEAR previous observations before changing configuration";
 } else if (!strcmp(op,"CONFIG")) {
  if(pc_value(token(&command),&a) && pc_value(token(&command),&b) && number(token(&command),&c) && number(token(&command),&d) && number(token(&command),&e) && c && d && e<2 && !token(&command)) {
   state.input_pc=a; state.observe_pc=b; state.every=c; state.timeout=d; state.player=e; state.configured=TRUE; ok=TRUE;
  }
 } else if (!strcmp(op,"RANGE")) {
  if(region(token(&command),&a) && number(token(&command),&b) && number(token(&command),&c) && c && c<=LT_BYTES-state.bytes && state.range_count<LT_RANGES && memory(a,b,c)) {
   state.ranges[state.range_count++]=(lt_range){a,b,c}; state.bytes+=c; ok=TRUE;
  }
 } else if (!strcmp(op,"CONDITION")) {
  char *r=token(&command);
  if(r && !strcmp(r,"OFF")) { state.condition=0; ok=TRUE; }
  else if(region(r,&a) && number(token(&command),&b) && number(token(&command),&c) && number(token(&command),&d) && c<=255 && d<=255 && memory(a,b,1)) {
   state.condition=1; state.cond_region=a; state.cond_address=b; state.cond_mask=c; state.cond_value=d; ok=TRUE;
  }
 } else if (!strcmp(op,"INPUT")) {
  if(number(token(&command),&a) && number(token(&command),&b) && a<=65535 && b && state.input_count<LT_INPUTS) {
   state.inputs[state.input_count++]=(lt_input){a,b}; ok=TRUE;
  }
 } else if (!strcmp(op,"START")) {
  if(number(token(&command),&a) && state.configured && state.input_count && !movie_active()) {
   if(!records) records=calloc(LT_RECORDS,sizeof(*records));
   if(records) {
    state.cursor=state.remaining=state.updates=state.count=state.hits=state.idle=0;
    state.target=a; state.active=TRUE; state.resume_skip=FALSE; state.status="running";
    cu_ctr_setslot_packet_override(state.player,0,TRUE,0,16);
    cu_avr_set_execution_hook(instruction); ok=TRUE;
   }
  }
 } else if (!strcmp(op,"CONTINUE")) {
  if(number(token(&command),&a) && records && state.configured && !movie_active() && (!a || a>state.count)) {
   state.target=a; state.active=TRUE; state.status="running";
   cu_ctr_setslot_packet_override(state.player,0,TRUE,state.mask,16);
   cu_avr_set_execution_hook(instruction); ok=TRUE;
  }
 } else if (!strcmp(op,"CHECKPOINT")) {
  char *action=token(&command);
  if(action && (!strcmp(action,"SAVE") || !strcmp(action,"LOAD"))) {
   char *path=command; if(path) path+=strspn(path," \t");
   if(path && *path && mainui_debug_is_paused() && !movie_active())
    ok=!strcmp(action,"SAVE")?lt_file_save(path):lt_file_load(path);
   snprintf(reply,capacity,ok?"{\"ok\":1}":"{\"ok\":0,\"error\":\"Logical checkpoint file failed (pause first; check identity, integrity and path)\"}");
   return;
  }
  if(action && number(token(&command),&a) && a<LT_CHECKPOINTS && !movie_active()) {
   lt_checkpoint *cp=&checkpoints[a];
   if(!strcmp(action,"TAKE")) {
    lt_checkpoint next={0}; next.state=state;
    next.sd_size=sd_size;
    if(sd_size) { next.sd=malloc(sd_size); if(next.sd) memcpy(next.sd,sd_image,sd_size); }
    if((!sd_size || next.sd) && (!state.count || (next.records=malloc(state.count*sizeof(*records))))) {
     if(state.count) memcpy(next.records,records,state.count*sizeof(*records));
     if(savestate_capture_to_mem(&next.machine)) {
      savestate_free_blob(&cp->machine); free(cp->records); free(cp->sd); *cp=next; ok=TRUE;
     }
    }
    if(!ok) { free(next.records); free(next.sd); }
   } else if(!strcmp(action,"RESTORE") && cp->machine.data) {
    if(!records) records=calloc(LT_RECORDS,sizeof(*records));
    uint8 *next_sd=cp->sd_size?malloc(cp->sd_size):NULL;
    if(next_sd) memcpy(next_sd,cp->sd,cp->sd_size);
    if(records && (!cp->sd_size || next_sd) && savestate_restore_from_mem(&cp->machine)) {
     state=cp->state; state.active=FALSE;
     if(state.count) memcpy(records,cp->records,state.count*sizeof(*records));
     state.status="checkpoint restored"; mainui_debug_set_paused(TRUE); ok=TRUE;
     sd_install(next_sd,cp->sd_size); next_sd=NULL;
    }
    free(next_sd);
   }
  }
 }
 snprintf(reply,capacity,ok?"{\"ok\":1}":"{\"ok\":0,\"error\":\"%s\"}",error);
}
