/* CZINPUT v2 transport. Independent 64 KiB LZ blocks bound memory and work;
 * no dictionary, library, or allocation is needed for individual events.
 * Included after the little-endian and CRC helpers in capture.c. */
#define MOVIE_BLOCK_SIZE 65536U
typedef struct {
 uint8 raw[MOVIE_BLOCK_SIZE];
 uint8 packed[MOVIE_BLOCK_SIZE];
 uint32 hash[4096];
 uint32 size, pos;
} movie_stream;

static uint32 movie_pack(movie_stream *s)
{
 uint32 i=0, out=0, literals=0, start=0;
 memset(s->hash, 0, sizeof(s->hash));
 while(i<s->size){
  uint32 match=0, distance=0;
  /* Abandon expansion early; the caller will store this block raw. */
  if(out+literals+4>=MOVIE_BLOCK_SIZE)return MOVIE_BLOCK_SIZE;
  if(s->size-i>=3){
   uint32 h=((uint32)s->raw[i]*251U+(uint32)s->raw[i+1]*31U+s->raw[i+2])&4095U;
   uint32 previous=s->hash[h];
   s->hash[h]=i+1;
   if(previous){
    uint32 p=previous-1;
    distance=i-p;
    while(match<130 && match<s->size-i && s->raw[p+match]==s->raw[i+match])match++;
   }
  }
  if(match>=3){
   if(literals){s->packed[out++]=(uint8)(literals-1);memcpy(s->packed+out,s->raw+start,literals);out+=literals;literals=0;}
   s->packed[out++]=(uint8)(128U+match-3);
   s->packed[out++]=(uint8)distance;s->packed[out++]=(uint8)(distance>>8);
   i+=match;
  }else{
   if(!literals)start=i;
   literals++;i++;
   if(literals==128){s->packed[out++]=127;memcpy(s->packed+out,s->raw+start,128);out+=128;literals=0;}
  }
 }
 if(literals){s->packed[out++]=(uint8)(literals-1);memcpy(s->packed+out,s->raw+start,literals);out+=literals;}
 return out;
}

static boole movie_unpack(movie_stream *s, uint32 stored)
{
 uint32 in=0,out=0;
 while(in<stored){
  uint32 token=s->packed[in++], length;
  if(token<128){
   length=token+1;
   if(length>stored-in || length>s->size-out)return FALSE;
   memcpy(s->raw+out,s->packed+in,length);in+=length;out+=length;
  }else{
   uint32 distance;
   length=(token&127U)+3;
   if(stored-in<2 || length>s->size-out)return FALSE;
   distance=s->packed[in]|((uint32)s->packed[in+1]<<8);in+=2;
   if(!distance || distance>out)return FALSE;
   /* Overlapping matches are intentional (e.g. a long run of zeros). */
   while(length--){s->raw[out]=s->raw[out-distance];out++;}
  }
 }
 return out==s->size;
}

static boole movie_flush(FILE *f, movie_stream *s)
{
 uint8 header[16];uint32 stored;uint8 const *data;
 if(!s || !s->size)return TRUE;
 stored=movie_pack(s);data=s->packed;
 if(stored>=s->size){stored=s->size;data=s->raw;}
 memcpy(header,"CMP2",4);put32(header+4,s->size);put32(header+8,stored);put32(header+12,crc(s->raw,s->size));
 if(fwrite(header,1,16,f)!=16 || fwrite(data,1,stored,f)!=stored)return FALSE;
 s->size=0;return TRUE;
}

static boole movie_write(FILE *f, movie_stream *s, uint8 const *data, uint32 size)
{
 if(!s)return !size || fwrite(data,1,size,f)==size;
 while(size){
  uint32 n=MOVIE_BLOCK_SIZE-s->size;if(n>size)n=size;
  memcpy(s->raw+s->size,data,n);s->size+=n;data+=n;size-=n;
  if(s->size==MOVIE_BLOCK_SIZE && !movie_flush(f,s))return FALSE;
 }
 return TRUE;
}

static boole movie_read(FILE *f, movie_stream *s, uint8 *data, uint32 size)
{
 if(!s)return !size || fread(data,1,size,f)==size;
 while(size){
  uint32 n;
  if(s->pos==s->size){
   uint8 header[16];uint32 stored;
   if(fread(header,1,16,f)!=16 || memcmp(header,"CMP2",4))return FALSE;
   s->size=get32(header+4);stored=get32(header+8);s->pos=0;
   if(!s->size || s->size>MOVIE_BLOCK_SIZE || !stored || stored>s->size)return FALSE;
   if(stored==s->size){if(fread(s->raw,1,stored,f)!=stored)return FALSE;}
   else if(fread(s->packed,1,stored,f)!=stored || !movie_unpack(s,stored))return FALSE;
   if(crc(s->raw,s->size)!=get32(header+12))return FALSE;
  }
  n=s->size-s->pos;if(n>size)n=size;
  memcpy(data,s->raw+s->pos,n);s->pos+=n;data+=n;size-=n;
 }
 return TRUE;
}

static boole movie_eof(FILE *f, movie_stream *s)
{
 return (!s || s->pos==s->size) && fgetc(f)==EOF && !ferror(f);
}
