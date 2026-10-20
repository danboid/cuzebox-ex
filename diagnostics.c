/* Bounded, opt-in hardware observations and failure bundles.
 * GPL-3.0-or-later. All entry points run on the emulation thread. */
#ifdef _WIN32
#define __USE_MINGW_ANSI_STDIO 1
#endif
#include "diagnostics.h"
#include "cu_avr.h"
#include "cu_spir.h"
#include "cu_ufile.h"
#include "savestate.h"
#include "capture.h"
#include "mainui.h"
#include "debug_source.h"
#include "logical_test.h"
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(p) _mkdir(p)
#else
#define MAKE_DIR(p) mkdir(p,0700)
#endif

#define EVENT_CAP 1024U
#define EXEC_CAP 4096U
#define INPUT_CAP 1024U
#define ALL_KINDS ((1U << DIAG_KIND_COUNT)-1U)
#define DEFAULT_TRIGGERS ((1U<<DIAG_WATCHDOG)|(1U<<DIAG_GUARD)|(1U<<DIAG_REPLAY)|(1U<<DIAG_ASSERT))
typedef struct {
 unsigned kind,pc,cycle,row,beam,detail,repeats;
 uint64_t sequence,frame;
} diagnostic_event;
typedef struct { unsigned pc,cycle,sp,opcode; } instruction_record;
typedef struct { unsigned port,slot,cycle,bits; uint64_t packet; } input_record;
static char const *names[DIAG_KIND_COUNT]={
 "spi_write_collision","spi_unread_receive","uart_overrun",
 "stack_high_write_with_interrupts","unusual_sync_interval","watchdog_reset",
 "guarded_memory_access","replay_failure","assertion_failure"
};
boole diagnostics_gate,diagnostics_recording;
static boole enabled,pending,spi_unread,sync_valid;
static unsigned warning_mask=ALL_KINDS,trigger_mask=DEFAULT_TRIGGERS,sync_cycle;
static unsigned instruction_pc;
static boole instruction_valid;
static diagnostic_event events[EVENT_CAP],trigger;
static instruction_record instructions[EXEC_CAP];
static input_record inputs[INPUT_CAP];
static uint64_t event_count,instruction_count,input_count,totals[DIAG_KIND_COUNT];
static savestate_blob_t checkpoints[2];
static unsigned checkpoint_next;
static uint64_t checkpoint_frame[2],checkpoint_execution[2],checkpoint_inputs[2];
static uint64_t last_frame=UINT64_MAX;
static char directory[1024],last_bundle[1280],status[160]="Disabled.";
static char last_warning[160]="No warnings recorded.";

static void update_gate(void) { diagnostics_gate=enabled||diagnostics_recording; }
boole diagnostics_enabled(void) { return enabled; }
boole diagnostics_pending(void) { return pending; }
char const *diagnostics_status(void) { return status; }
char const *diagnostics_last_warning(void) { return last_warning; }
void diagnostics_enable(boole enable)
{
 enabled=enable; spi_unread=FALSE; sync_valid=FALSE; update_gate();
 if(!diagnostics_recording) snprintf(status,sizeof(status),enable?"Warnings enabled.":"Warnings disabled.");
}
static void free_checkpoints(void)
{
 for(unsigned i=0;i<2;i++) savestate_free_blob(&checkpoints[i]);
 checkpoint_next=0; last_frame=UINT64_MAX;
}
void diagnostics_disarm(void)
{
 diagnostics_recording=FALSE; pending=FALSE; free_checkpoints(); update_gate();
 snprintf(status,sizeof(status),"Disarmed.");
}
void diagnostics_machine_changed(void)
{
 /* Never mix ROMs, resets, or externally restored timelines in one bundle. */
 free_checkpoints(); event_count=instruction_count=input_count=0;
 memset(totals,0,sizeof(totals)); spi_unread=FALSE; sync_valid=FALSE;
 instruction_valid=FALSE;
 pending=FALSE;
 snprintf(last_warning,sizeof(last_warning),"No warnings recorded.");
}
void diagnostics_peripheral_reset(void) { spi_unread=FALSE; sync_valid=FALSE; }
void diagnostics_event(unsigned kind,unsigned detail)
{
 cu_state_cpu_t *cpu;
 diagnostic_event event;
 if(!diagnostics_gate || pending || kind>=DIAG_KIND_COUNT) return;
 if(!(warning_mask & (1U<<kind)) && !(diagnostics_recording && (trigger_mask & (1U<<kind)))) return;
 cpu=cu_avr_get_state();
 memset(&event,0,sizeof(event));
 event.kind=kind; event.pc=cpu->pc & 0x7fffU; event.cycle=cpu->cycle;
 /* CPU PC may already point at the next instruction while handling I/O. */
 if(instruction_valid) event.pc=instruction_pc;
 event.frame=mainui_get_frame_counter(); event.detail=detail;
 event.row=cu_avr_get_video_beam_pulse(); event.beam=cu_avr_get_video_beam_cycle();
 event.repeats=1; totals[kind]++;
 snprintf(last_warning,sizeof(last_warning),"%s PC=%04X cycle=%u",names[kind],event.pc,event.cycle);
 if(event_count) {
  diagnostic_event *last=&events[(event_count-1)%EVENT_CAP];
  if(last->kind==kind && last->pc==event.pc && last->detail==detail && last->frame==event.frame && last->repeats<UINT_MAX) {
   last->repeats++;
   event.sequence=last->sequence;
  } else { event.sequence=event_count; events[event_count++%EVENT_CAP]=event; }
 } else { event.sequence=event_count; events[event_count++%EVENT_CAP]=event; }
 if(diagnostics_recording && (trigger_mask & (1U<<kind))) { trigger=event; pending=TRUE; }
}
void diagnostics_instruction(unsigned pc)
{
 cu_state_cpu_t *cpu=cu_avr_get_state();
 instruction_pc=pc; instruction_valid=TRUE;
 if(!diagnostics_recording || pending) return;
 instruction_record *r=&instructions[instruction_count++%EXEC_CAP];
 r->pc=pc; r->cycle=cpu->cycle;
 r->sp=cpu->iors[CU_IO_SPL] | (cpu->iors[CU_IO_SPH]<<8);
 r->opcode=cpu->crom[pc*2] | (cpu->crom[pc*2+1]<<8);
}
void diagnostics_spi_complete(void)
{
 if(spi_unread) diagnostics_event(DIAG_SPI_UNREAD,cu_avr_get_state()->iors[CU_IO_SPDR]);
 spi_unread=TRUE;
}
void diagnostics_spi_read(void) { spi_unread=FALSE; }
void diagnostics_sync(unsigned cycle)
{
 unsigned elapsed=(uint32)(cycle-sync_cycle);
 if(sync_valid && !((elapsed>=800 && elapsed<=1000)||(elapsed>=1700 && elapsed<=1950)))
  diagnostics_event(DIAG_SYNC,elapsed);
 sync_cycle=cycle; sync_valid=TRUE;
}
void diagnostics_input(unsigned port,unsigned slot,uint64_t packet,unsigned bits)
{
 if(!diagnostics_recording || pending) return;
 input_record *r=&inputs[input_count++%INPUT_CAP];
 r->port=port; r->slot=slot; r->packet=packet; r->bits=bits; r->cycle=cu_avr_getcycle();
}
static boole take_checkpoint(void)
{
 savestate_blob_t next={0};
 /* Bound snapshot memory even when a cartridge requests very large SPI RAM. */
 if(cu_spir_get_state()->size>16U*1024U*1024U) {
  snprintf(status,sizeof(status),"Recorder requires SPI RAM <= 16 MiB."); return FALSE;
 }
 if(!savestate_capture_to_mem(&next)) { snprintf(status,sizeof(status),"Checkpoint allocation failed."); return FALSE; }
 if(next.size>32U*1024U*1024U) {
  savestate_free_blob(&next); snprintf(status,sizeof(status),"Checkpoint exceeds 32 MiB limit."); return FALSE;
 }
 savestate_free_blob(&checkpoints[checkpoint_next]); checkpoints[checkpoint_next]=next;
 checkpoint_frame[checkpoint_next]=mainui_get_frame_counter();
 checkpoint_execution[checkpoint_next]=instruction_count;
 checkpoint_inputs[checkpoint_next]=input_count;
 checkpoint_next^=1;
 last_frame=mainui_get_frame_counter();
 return TRUE;
}
boole diagnostics_arm(char const *path)
{
 struct stat st;
 if(!path || !*path || strlen(path)>=sizeof(directory) || pending) return FALSE;
 if(stat(path,&st)!=0 && MAKE_DIR(path)!=0) {
  snprintf(status,sizeof(status),"Cannot create failure output directory."); return FALSE;
 }
 if(stat(path,&st)!=0 || !(st.st_mode & S_IFDIR)) {
  snprintf(status,sizeof(status),"Failure output path is not a directory."); return FALSE;
 }
 diagnostics_disarm(); diagnostics_machine_changed();
 snprintf(directory,sizeof(directory),"%s",path);
 last_bundle[0]=0;
 if(!take_checkpoint()) return FALSE;
 diagnostics_recording=TRUE; update_gate();
 snprintf(status,sizeof(status),"Armed; bounded history, one bundle per arm."); return TRUE;
}
static void json_string(FILE *f,char const *s)
{
 if(!s) s="";
 fputc('"',f);
 for(;*s;s++) {
  unsigned c=(unsigned char)*s;
  if(c=='"' || c=='\\') { fputc('\\',f); fputc(c,f); }
  else if(c<32) fprintf(f,"\\u%04x",c);
  else fputc(c,f);
 }
 fputc('"',f);
}
static void event_json(FILE *f,diagnostic_event const *e)
{
 cu_debug_source_location_t loc;
 fprintf(f,"{\"sequence\":%llu,\"kind\":\"%s\",\"pc_word\":%u,\"cycle\":%u,\"frame\":%llu,\"row\":%u,\"beam\":%u,\"detail\":%u,\"repeats\":%u",
  (unsigned long long)e->sequence,names[e->kind],e->pc,e->cycle,(unsigned long long)e->frame,e->row,e->beam,e->detail,e->repeats);
 if(cu_debug_source_lookup(e->pc,&loc)) {
  fprintf(f,",\"source_line\":%u,\"source_file\":",loc.line);
  json_string(f,cu_debug_source_file_path(loc.file_index));
 }
 fputs("}",f);
}
static FILE *bundle_open(char const *name)
{
 char path[1400];
 snprintf(path,sizeof(path),"%s/%s",last_bundle,name);
 return fopen(path,"wb");
}
static boole close_file(FILE *f) { boole ok=!ferror(f); if(fclose(f)!=0) ok=FALSE; return ok; }
static boole bytes_file(char const *name,void const *data,size_t size)
{
 FILE *f=bundle_open(name); if(!f) return FALSE;
 boole ok=fwrite(data,1,size,f)==size; return close_file(f)&&ok;
}
static void put32(uint8 *p,unsigned n) { for(unsigned i=0;i<4;i++) p[i]=(uint8)(n>>(i*8)); }
static unsigned get32(uint8 const *p) { return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24); }
static boole state_file(char const *name,savestate_blob_t const *blob)
{
 uint8 header[24]={ 'C','Z','D','I','A','G','1',0 };
 FILE *f=bundle_open(name); if(!f) return FALSE;
 put32(header+8,CAPTURE_BEHAVIOR_ID); put32(header+12,mainui_get_current_rom_crc32());
 put32(header+16,blob->size); put32(header+20,cu_ufile_crc32(blob->data,blob->size));
 boole ok=fwrite(header,1,sizeof(header),f)==sizeof(header) &&
  fwrite(blob->data,1,blob->size,f)==blob->size;
 return close_file(f)&&ok;
}
static boole restore_file(char const *path)
{
 uint8 header[24]; savestate_blob_t blob={0}; boole ok=FALSE;
 if(!mainui_debug_is_paused() || diagnostics_recording || pending || logical_test_active()) return FALSE;
#ifdef ENABLE_ICAP
 if(capture_is_active()) return FALSE;
#endif
 FILE *f=fopen(path,"rb"); if(!f) return FALSE;
 if(fread(header,1,24,f)!=24 || memcmp(header,"CZDIAG1",8) ||
    get32(header+8)!=CAPTURE_BEHAVIOR_ID || get32(header+12)!=mainui_get_current_rom_crc32()) goto done;
 blob.size=get32(header+16);
 if(!blob.size || blob.size>32U*1024U*1024U) goto done;
 blob.data=(uint8*)malloc(blob.size);
 if(!blob.data || fread(blob.data,1,blob.size,f)!=blob.size || fgetc(f)!=EOF || ferror(f)) goto done;
 if(cu_ufile_crc32(blob.data,blob.size)!=get32(header+20)) goto done;
 ok=savestate_restore_from_mem(&blob);
 if(ok) diagnostics_machine_changed();
done:
 fclose(f); savestate_free_blob(&blob); return ok;
}
static boole write_bundle(void)
{
 static unsigned serial;
 boole ok=TRUE,screenshot=FALSE;
 unsigned attempts,oldest=checkpoints[checkpoint_next].data?checkpoint_next:(checkpoint_next^1);
 savestate_blob_t final={0};
 uint8 *bmp=NULL; auint size=0,w=0,h=0;
 FILE *f;
 for(attempts=0;attempts<100;attempts++) {
  snprintf(last_bundle,sizeof(last_bundle),"%s/failure-%llu-%u",directory,(unsigned long long)time(NULL),serial++);
  if(MAKE_DIR(last_bundle)==0) break;
  if(errno!=EEXIST) return FALSE;
 }
 if(attempts==100) return FALSE;
 if(checkpoints[oldest].data)
  ok=state_file("before.state",&checkpoints[oldest])&&ok;
 if(savestate_capture_to_mem(&final)) {
  ok=state_file("failure.state",&final)&&ok; savestate_free_blob(&final);
 } else ok=FALSE;
 ok=bytes_file("flash.bin",cu_avr_get_state()->crom,65536)&&ok;
 if(mainui_capture_screenshot_bmp(&bmp,&size,&w,&h)) {
  screenshot=bytes_file("screen.bmp",bmp,size); free(bmp);
  ok=screenshot&&ok;
 }
 f=bundle_open("events.jsonl");
 if(f) {
  uint64_t first=event_count>EVENT_CAP?event_count-EVENT_CAP:0;
  for(uint64_t i=first;i<event_count;i++) { event_json(f,&events[i%EVENT_CAP]); fputc('\n',f); }
  ok=close_file(f)&&ok;
 } else ok=FALSE;
 f=bundle_open("execution.jsonl");
 if(f) {
  uint64_t first=instruction_count>EXEC_CAP?instruction_count-EXEC_CAP:0;
  for(uint64_t i=first;i<instruction_count;i++) {
   instruction_record *r=&instructions[i%EXEC_CAP];
   fprintf(f,"{\"sequence\":%llu,\"pc_word\":%u,\"cycle\":%u,\"sp\":%u,\"opcode\":%u}\n",(unsigned long long)i,r->pc,r->cycle,r->sp,r->opcode);
  }
  ok=close_file(f)&&ok;
 } else ok=FALSE;
 f=bundle_open("inputs.jsonl");
 if(f) {
  uint64_t first=input_count>INPUT_CAP?input_count-INPUT_CAP:0;
  for(uint64_t i=first;i<input_count;i++) {
   input_record *r=&inputs[i%INPUT_CAP];
   fprintf(f,"{\"sequence\":%llu,\"cycle\":%u,\"port\":%u,\"slot\":%u,\"bits\":%u,\"packet\":\"%016llx\"}\n",
    (unsigned long long)i,r->cycle,r->port,r->slot,r->bits,(unsigned long long)r->packet);
  }
  ok=close_file(f)&&ok;
 } else ok=FALSE;
 f=bundle_open("manifest.json");
 if(!f) return FALSE;
 fprintf(f,"{\"format\":\"CUZEBOX_FAILURE_1\",\"complete\":%s,\"behavior_id\":%u,\"rom_crc32\":%u,\"rom_path\":",
  ok?"true":"false",CAPTURE_BEHAVIOR_ID,mainui_get_current_rom_crc32());
 json_string(f,mainui_get_loaded_rom_path());
 fputs(",\"trigger\":",f); event_json(f,&trigger);
 fprintf(f,",\"checkpoint_frame\":%llu,\"checkpoint_execution_sequence\":%llu,\"checkpoint_input_sequence\":%llu,"
  "\"execution_dropped\":%llu,\"input_dropped\":%llu,\"events_dropped\":%llu,\"screenshot\":%s,"
  "\"replay_complete\":false,\"settings\":{\"interlace_mode\":%u,\"interlace_method\":%u,\"tv_sync_pll\":%u,"
  "\"tv_sync_recovery\":%u,\"tv_sync_strength\":%u,\"serial_route\":%u,\"resident_bootloader\":%u,\"spi_ram_bytes\":%u},",
  (unsigned long long)checkpoint_frame[oldest],(unsigned long long)checkpoint_execution[oldest],(unsigned long long)checkpoint_inputs[oldest],
  (unsigned long long)(instruction_count>EXEC_CAP?instruction_count-EXEC_CAP:0),
  (unsigned long long)(input_count>INPUT_CAP?input_count-INPUT_CAP:0),
  (unsigned long long)(event_count>EVENT_CAP?event_count-EVENT_CAP:0),screenshot?"true":"false",
  mainui_get_interlace_mode(),mainui_get_interlace_presentation(),mainui_get_tv_sync_pll(),
  mainui_get_tv_sync_recovery(),mainui_get_tv_sync_strength(),mainui_get_serial_route(),
  mainui_get_resident_bootloader_enable(),cu_spir_get_state()->size);
 cu_spisd_model_t sd; mainui_get_sd_timing_model(&sd);
#ifdef ENABLE_ICAP
 fputs("\"movie_status\":",f); json_string(f,capture_get_status());
 fputs(",\"movie_path\":",f); json_string(f,capture_get_filename());
 fprintf(f,",\"movie_frame\":%llu,",(unsigned long long)capture_frame_number());
#endif
 fprintf(f,"\"sd_model\":{\"preset\":%u,\"init_ms\":%u,\"cmd_wait_bytes\":%u,\"read_wait_bytes\":%u,"
  "\"write_busy_ms\":%u,\"cs_high_ms\":%u,\"init_min_byte_cycles\":%u,\"init_max_byte_cycles\":%u}}\n",
  sd.preset,sd.init_ms,sd.cmd_wait_bytes,sd.read_wait_bytes,sd.write_busy_ms,sd.cs_high_ms,sd.init_min_byte_cycles,sd.init_max_byte_cycles);
 return close_file(f)&&ok;
}
void diagnostics_service(void)
{
 if(pending) {
  mainui_debug_set_paused(TRUE);
  boole ok=write_bundle();
  diagnostics_disarm();
  snprintf(status,sizeof(status),ok?"Failure bundle saved; emulation paused.":"Failure bundle incomplete; check output directory/disk.");
 } else if(diagnostics_recording && last_frame!=mainui_get_frame_counter()) {
  if(!take_checkpoint()) {
   char error[sizeof(status)]; memcpy(error,status,sizeof(error));
   diagnostics_disarm(); memcpy(status,error,sizeof(status));
  }
 }
}
static boole number(char const *s,unsigned *out)
{
 char *end; unsigned long n;
 if(!s || !*s || *s=='-') return FALSE;
 errno=0; n=strtoul(s,&end,0);
 if(errno || *end || n>UINT_MAX) return FALSE;
 *out=(unsigned)n; return TRUE;
}
void diagnostics_command(char *args,char *reply,unsigned capacity)
{
 char op[24]={0}; unsigned a,b; boole ok=TRUE;
 if(!args) args="";
 while(*args==' ') args++;
 unsigned n=0; while(*args && *args!=' ' && n<sizeof(op)-1) op[n++]=*args++;
 while(*args==' ') args++;
 if(!strcmp(op,"STATUS") || !op[0]) {
  /* Paths/status are returned as hex to avoid truncation and JSON escaping. */
  char hex[sizeof(last_bundle)*2+1]; size_t i;
  for(i=0;last_bundle[i];i++) snprintf(hex+i*2,3,"%02x",(unsigned char)last_bundle[i]);
  hex[i*2]=0;
  snprintf(reply,capacity,"{\"ok\":1,\"enabled\":%u,\"armed\":%u,\"pending\":%u,\"mask\":%u,\"trigger_mask\":%u,\"events\":%llu,\"instructions\":%llu,\"inputs\":%llu,\"first_event\":%llu,\"bundle_path_hex\":\"%s\",\"status\":\"%s\"}",
   enabled,diagnostics_recording,pending,warning_mask,trigger_mask,(unsigned long long)event_count,
   (unsigned long long)instruction_count,(unsigned long long)input_count,
   (unsigned long long)(event_count>EVENT_CAP?event_count-EVENT_CAP:0),hex,status); return;
 } else if(!strcmp(op,"ENABLE")) diagnostics_enable(TRUE);
 else if(!strcmp(op,"DISABLE")) diagnostics_enable(FALSE);
 else if(!strcmp(op,"DISARM")) diagnostics_disarm();
 else if(!strcmp(op,"ARM")) ok=diagnostics_arm(args);
 else if(!strcmp(op,"RESTORE")) ok=restore_file(args);
 else if(!strcmp(op,"CLEAR")) { if(diagnostics_recording || pending) ok=FALSE; else diagnostics_machine_changed(); }
 else if(!strcmp(op,"MASK")) { ok=number(args,&a)&&a<=ALL_KINDS; if(ok) warning_mask=a; }
 else if(!strcmp(op,"TRIGGERS")) { ok=number(args,&a)&&a<=ALL_KINDS; if(ok) trigger_mask=a; }
 else if(!strcmp(op,"TRIGGER")) {
  ok=diagnostics_recording&&!pending&&number(args,&a);
  if(ok) {
   unsigned saved=trigger_mask; trigger_mask|=1U<<DIAG_ASSERT;
   diagnostics_event(DIAG_ASSERT,a); trigger_mask=saved; diagnostics_service();
  }
 } else if(!strcmp(op,"READ")) {
  /* One bounded record per request; sequence is stable until CLEAR/re-arm. */
  ok=number(args,&a);
  if(ok && a<event_count && event_count-a<=EVENT_CAP) {
   diagnostic_event *e=&events[a%EVENT_CAP];
   cu_debug_source_location_t loc; b=cu_debug_source_lookup(e->pc,&loc)?loc.line:0;
   snprintf(reply,capacity,"{\"ok\":1,\"kind\":\"%s\",\"pc_word\":%u,\"cycle\":%u,\"frame\":%llu,\"row\":%u,\"beam\":%u,\"detail\":%u,\"repeats\":%u,\"source_line\":%u,\"source_file_index\":%u}",
    names[e->kind],e->pc,e->cycle,(unsigned long long)e->frame,e->row,e->beam,e->detail,e->repeats,b,b?loc.file_index:0); return;
  } else ok=FALSE;
 } else ok=FALSE;
 snprintf(reply,capacity,ok?"{\"ok\":1}":"{\"ok\":0,\"error\":\"Invalid diagnostics operation, argument, state, or output directory\"}");
}
