/* Versioned logical checkpoints: explicit little-endian fields, no pointers.
 * Embedded machine blobs retain the savestate ABI/host compatibility checks. */
#define LT_SCALARS(X) \
 X(input_pc) X(observe_pc) X(every) X(timeout) X(player) \
 X(range_count) X(bytes) X(input_count) X(cursor) X(remaining) X(updates) X(count) \
 X(hits) X(idle) X(target) X(condition) X(cond_region) X(cond_address) \
 X(cond_mask) X(cond_value) X(mask)

static void lt_put(uint8 **p,unsigned n)
{ for(unsigned i=0;i<4;i++) *(*p)++=(uint8)(n>>(8*i)); }
static unsigned lt_get(uint8 const **p)
{ unsigned n=0; for(unsigned i=0;i<4;i++) n|=(unsigned)*(*p)++<<(8*i); return n; }

static boole lt_file_save(char const *path)
{
 savestate_blob_t blob={0}; boole ok=FALSE;
 filesys_flush(FILESYS_CH_SD);
 if(!state.configured || !state.input_count || !savestate_capture_to_mem(&blob)) return FALSE;
 size_t length=23*4 + state.range_count*12 + state.input_count*8 +
               state.count*(20+state.bytes) + 8 + blob.size + sd_size;
 uint8 *data=malloc(length), *p=data;
 if(data) {
#define PUT(name) lt_put(&p,state.name);
  LT_SCALARS(PUT)
#undef PUT
  lt_put(&p,state.configured); lt_put(&p,state.resume_skip);
  for(unsigned i=0;i<state.range_count;i++) {
   lt_put(&p,state.ranges[i].region); lt_put(&p,state.ranges[i].address); lt_put(&p,state.ranges[i].length);
  }
  for(unsigned i=0;i<state.input_count;i++) { lt_put(&p,state.inputs[i].mask); lt_put(&p,state.inputs[i].hits); }
  for(unsigned i=0;i<state.count;i++) {
   lt_record *r=&records[i];
   lt_put(&p,r->pc); lt_put(&p,r->cycle); lt_put(&p,r->row); lt_put(&p,r->beam); lt_put(&p,r->update);
   memcpy(p,r->bytes,state.bytes); p+=state.bytes;
  }
  lt_put(&p,blob.size); memcpy(p,blob.data,blob.size); p+=blob.size;
  lt_put(&p,sd_size); if(sd_size) memcpy(p,sd_image,sd_size);
  uint8 header[24], *h=header;
  lt_put(&h,0x3154434cU); lt_put(&h,CAPTURE_BEHAVIOR_ID);
  lt_put(&h,cu_ufile_crc32(cu_avr_get_state()->crom,65536));
  lt_put(&h,(unsigned)length); lt_put(&h,cu_ufile_crc32(data,(auint)length)); lt_put(&h,1);
  int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_BINARY,0666);
  FILE *f=fd<0?NULL:fdopen(fd,"wb"); if(fd>=0 && !f) close(fd);
  if(f) { ok=fwrite(header,1,24,f)==24 && fwrite(data,1,length,f)==length; if(fclose(f)) ok=FALSE; }
  free(data);
 }
 savestate_free_blob(&blob); return ok;
}

static boole lt_file_load(char const *path)
{
 FILE *f=fopen(path,"rb"); uint8 header[24]; boole ok=FALSE;
 uint8 *data=NULL, *next_sd=NULL; lt_record *next_records=NULL; lt_state next={0};
 if(!f) return FALSE;
 if(fread(header,1,24,f)!=24) goto done;
 uint8 const *h=header;
 if(lt_get(&h)!=0x3154434cU || lt_get(&h)!=CAPTURE_BEHAVIOR_ID ||
    lt_get(&h)!=cu_ufile_crc32(cu_avr_get_state()->crom,65536)) goto done;
 unsigned length=lt_get(&h), crc=lt_get(&h);
 if(lt_get(&h)!=1 || length<100 || length>64U*1024U*1024U) goto done;
 data=malloc(length); if(!data || fread(data,1,length,f)!=length || fgetc(f)!=EOF ||
    cu_ufile_crc32(data,length)!=crc) goto done;
 uint8 const *p=data;
#define GET(name) next.name=lt_get(&p);
 LT_SCALARS(GET)
#undef GET
 unsigned configured=lt_get(&p), resume_skip=lt_get(&p);
 if(configured!=1 || resume_skip>1) goto done;
 next.configured=TRUE; next.resume_skip=resume_skip?TRUE:FALSE;
 if(next.input_pc>=32768 || next.observe_pc>=32768 || !next.every || !next.timeout ||
    next.player>1 || next.range_count>LT_RANGES || next.bytes>LT_BYTES ||
    !next.input_count || next.input_count>LT_INPUTS || next.cursor>next.input_count ||
    next.count>LT_RECORDS || next.hits>=next.every || next.condition>1 || next.cond_region>1 ||
    next.cond_mask>255 || next.cond_value>255 || next.mask>65535) goto done;
 size_t prefix=92+next.range_count*12+next.input_count*8+next.count*(20+next.bytes)+4;
 if(prefix>length) goto done;
 unsigned width=0;
 for(unsigned i=0;i<next.range_count;i++) {
  lt_range *r=&next.ranges[i]; r->region=lt_get(&p); r->address=lt_get(&p); r->length=lt_get(&p);
  if(r->region>1 || !r->length || r->length>LT_BYTES-width || !memory(r->region,r->address,r->length)) goto done;
  width+=r->length;
 }
 if(width!=next.bytes || (next.condition && !memory(next.cond_region,next.cond_address,1))) goto done;
 for(unsigned i=0;i<next.input_count;i++) {
  next.inputs[i].mask=lt_get(&p); next.inputs[i].hits=lt_get(&p);
  if(next.inputs[i].mask>65535 || !next.inputs[i].hits) goto done;
 }
 if((!next.cursor && next.remaining) || (next.cursor && next.remaining>=next.inputs[next.cursor-1].hits)) goto done;
 next_records=calloc(LT_RECORDS,sizeof(*next_records)); if(!next_records) goto done;
 for(unsigned i=0;i<next.count;i++) {
  lt_record *r=&next_records[i]; r->pc=lt_get(&p); r->cycle=lt_get(&p); r->row=lt_get(&p);
  r->beam=lt_get(&p); r->update=lt_get(&p); memcpy(r->bytes,p,next.bytes); p+=next.bytes;
  if(r->pc>=32768 || r->update>next.updates) goto done;
 }
 unsigned size=lt_get(&p);
 savestate_blob_t blob={(uint8*)p,size};
 if(length-prefix<4 || size>length-prefix-4 || !savestate_validate_blob(&blob)) goto done;
 p+=size; unsigned next_size=lt_get(&p);
 if(next_size>8U*1024U*1024U || next_size%512 || next_size!=length-prefix-size-4) goto done;
 if(next_size) { next_sd=malloc(next_size); if(!next_sd) goto done; memcpy(next_sd,p,next_size); }
 if(!savestate_restore_from_mem(&blob)) goto done;
 free(records); records=next_records; next_records=NULL;
 state=next; state.active=FALSE; state.status="checkpoint loaded";
 sd_install(next_sd,next_size); next_sd=NULL;
 mainui_debug_set_paused(TRUE); ok=TRUE;
done:
 free(next_sd); free(next_records); free(data); fclose(f); return ok;
}
#undef LT_SCALARS
