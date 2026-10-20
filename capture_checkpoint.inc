/* Replay checkpoints include private media and the partially decoded block.
 * Included in capture.c; no native pointers are persisted to disk. */
#define MOVIE_CHECKPOINTS 4U
typedef struct {
 savestate_blob_t machine;
 movie_stream *stream;
 movie_sector *sectors;
 uint8 *next;
 uint32 next_size, cycle, sd_digest, file_crc, rom_crc;
 uint64_t time, frames, rom_size;
 long offset;
 unsigned sector_count;
 char type[4], name[4096];
 uint8 topology[MOVIE_TOPO_SIZE];
} movie_checkpoint;
static movie_checkpoint movie_checkpoints[MOVIE_CHECKPOINTS];
static boole checkpoint_reopening, movie_seeking;
static uint64_t movie_seek_target;

static void checkpoint_free(movie_checkpoint *c)
{
 savestate_free_blob(&c->machine); free(c->stream); free(c->sectors); free(c->next);
 memset(c,0,sizeof(*c));
}
void capture_checkpoints_clear(void)
{
 for(unsigned i=0;i<MOVIE_CHECKPOINTS;i++) checkpoint_free(&movie_checkpoints[i]);
 movie_seeking=FALSE;
}
static boole file_checksum(char const *name,uint32 *result)
{
 FILE *f=fopen(name,"rb"); uint8 bytes[8192]; size_t n; uint32 sum=0xffffffffU;
 if(!f) return FALSE;
 while((n=fread(bytes,1,sizeof(bytes),f))) sum=crc_bytes(sum,bytes,n);
 boole ok=!ferror(f); fclose(f); *result=~sum; return ok;
}
boole capture_checkpoint_take(unsigned slot)
{
 movie_checkpoint next={0}; unsigned j=0;
 if(slot>=MOVIE_CHECKPOINTS || !capture_playing || capture_failed) return FALSE;
 next.offset=ftell(capture_file);
 if(next.offset<0 || !file_checksum(capture_name,&next.file_crc) ||
    !identity(&next.rom_crc,&next.rom_size)) return FALSE;
 next.next=malloc(capture_next_size ? capture_next_size : 1);
 if(capture_stream) next.stream=malloc(sizeof(*next.stream));
 if(capture_sector_count) next.sectors=malloc(capture_sector_count*sizeof(*next.sectors));
 if(!next.next || (capture_stream && !next.stream) || (capture_sector_count && !next.sectors) ||
    !savestate_capture_to_mem(&next.machine)) { checkpoint_free(&next); return FALSE; }
 memcpy(next.next,capture_next,capture_next_size); next.next_size=capture_next_size;
 if(capture_stream) *next.stream=*capture_stream;
 for(unsigned i=0;i<1024;i++) for(movie_sector *s=capture_sectors[i];s;s=s->next) next.sectors[j++]=*s;
 next.sector_count=j; next.cycle=capture_cycle; next.time=capture_time;
 next.frames=capture_frames; next.sd_digest=capture_sd_digest;
 memcpy(next.type,capture_next_type,4); memcpy(next.topology,capture_topology,sizeof(next.topology));
 strcpy(next.name,capture_name);
 checkpoint_free(&movie_checkpoints[slot]); movie_checkpoints[slot]=next;
 return TRUE;
}
boole capture_checkpoint_restore(unsigned slot)
{
 uint32 sum,rom; uint64_t size;
 movie_sector *cache[1024]={0};
 if(slot>=MOVIE_CHECKPOINTS) return FALSE;
 movie_checkpoint *c=&movie_checkpoints[slot];
 if(!c->machine.data || !file_checksum(c->name,&sum) || sum!=c->file_crc ||
    !identity(&rom,&size) || rom!=c->rom_crc || size!=c->rom_size) return FALSE;
 /* Allocate the replacement cache before changing live replay state. */
 for(unsigned i=0;i<c->sector_count;i++) {
  movie_sector *s=malloc(sizeof(*s));
  if(!s) goto bad;
  *s=c->sectors[i]; unsigned bucket=s->number%1024;
  s->next=cache[bucket]; cache[bucket]=s;
 }
 if(!capture_playing || strcmp(capture_name,c->name)) {
  checkpoint_reopening=TRUE;
  boole ok=capture_start_ex(c->name,TRUE,FALSE);
  checkpoint_reopening=FALSE;
  if(!ok) goto bad;
 }
 if(capture_next_capacity<c->next_size) {
  uint8 *p=realloc(capture_next,c->next_size);
  if(!p) goto bad;
  capture_next=p; capture_next_capacity=c->next_size;
 }
 if(fseek(capture_file,c->offset,SEEK_SET) || !savestate_restore_replay_from_mem(&c->machine)) goto bad;
 if(c->stream) *capture_stream=*c->stream;
 memcpy(capture_next,c->next,c->next_size); capture_next_size=c->next_size;
 memcpy(capture_next_type,c->type,4); memcpy(capture_topology,c->topology,sizeof(c->topology));
 for(unsigned i=0;i<1024;i++) {
  movie_sector *s=capture_sectors[i];
  while(s) { movie_sector *next=s->next; free(s); s=next; }
  capture_sectors[i]=cache[i];
 }
 capture_sector_count=c->sector_count; capture_cycle=c->cycle; capture_time=c->time;
 capture_frames=c->frames; capture_sd_digest=c->sd_digest;
 capture_failed=capture_sd_busy=capture_topology_dirty=movie_seeking=FALSE;
 capture_status="Replay checkpoint restored.";
 return TRUE;
bad:
 for(unsigned i=0;i<1024;i++) { movie_sector *s=cache[i]; while(s) { movie_sector *n=s->next; free(s); s=n; } }
 return FALSE;
}
uint64_t capture_frame_number(void) { return capture_frames; }
boole capture_seek(uint64_t frame)
{
 unsigned best=MOVIE_CHECKPOINTS;
 if(frame>capture_total_frames) return FALSE;
 for(unsigned i=0;i<MOVIE_CHECKPOINTS;i++) {
  movie_checkpoint *c=&movie_checkpoints[i];
  if(c->machine.data && c->frames<=frame && (best==MOVIE_CHECKPOINTS || c->frames>movie_checkpoints[best].frames)) best=i;
 }
 if(best==MOVIE_CHECKPOINTS || !capture_checkpoint_restore(best)) return FALSE;
 movie_seek_target=frame; movie_seeking=capture_frames<frame;
 mainui_debug_set_paused(!movie_seeking);
 return TRUE;
}
