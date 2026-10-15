#include "filesys.h"
#include "cu_vfat.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ROOT_BASE 0x040200U
#define DATA_BYTES CU_VFAT_SYS_SIZE
#define CL_BYTES 0x8000U

static void path_join(char *d, size_t n, const char *a, const char *b){ snprintf(d,n,"%s/%s",a,b); }
static void must_mkdir(const char *p){ assert(mkdir(p,0777)==0); }
static void must_file(const char *p, const char *s){ FILE *f=fopen(p,"wb"); assert(f); assert(fwrite(s,1,strlen(s),f)==strlen(s)); fclose(f); }
static long long fsize(const char *p){ struct stat st; return stat(p,&st)==0 ? (long long)st.st_size : -1; }
static int isdir(const char *p){ struct stat st; return stat(p,&st)==0 && S_ISDIR(st.st_mode); }

static unsigned root_slot(const char *host)
{
 cu_state_vfat_t *st=cu_vfat_get_state(); unsigned i;
 for(i=0;i<CU_VFAT_ROOT_SIZE;i++) if(strcmp(st->hnames[i],host)==0) return i;
 assert(!"root slot missing"); return 0;
}
static unsigned ent_start(const unsigned char *e){ return (unsigned)e[26] | ((unsigned)e[27]<<8); }
static unsigned cl_pos(unsigned cl){ return DATA_BYTES + (cl-2U)*CL_BYTES; }
static void read_sector(unsigned pos, unsigned char *b){ unsigned i; for(i=0;i<512;i++) b[i]=(unsigned char)cu_vfat_read(pos+i); }
static void write_sector(unsigned pos, const unsigned char *b){ unsigned i; for(i=0;i<512;i++) cu_vfat_write(pos+i,b[i]); }
static void fat83(unsigned char *d,const char *base,const char *ext){ size_t i; memset(d,' ',11); for(i=0;i<8&&base[i];i++)d[i]=(unsigned char)base[i]; for(i=0;i<3&&ext[i];i++)d[8+i]=(unsigned char)ext[i]; }
static void make_ent(unsigned char *e,const char *base,const char *ext,unsigned attr,unsigned cl,unsigned size)
{
 memset(e,0,32); fat83(e,base,ext); e[11]=(unsigned char)attr; e[26]=cl&255; e[27]=(cl>>8)&255;
 e[28]=size&255; e[29]=(size>>8)&255; e[30]=(size>>16)&255; e[31]=(size>>24)&255;
}
static unsigned find_free_cluster(void)
{
 cu_state_vfat_t *st=cu_vfat_get_state(); unsigned cl;
 for(cl=2;cl<0xFFF0U;cl++) if(st->sys[0x200U+cl*2U]==0 && st->sys[0x201U+cl*2U]==0) return cl;
 assert(!"no free cluster"); return 0;
}
static void alloc_one(unsigned cl)
{
 cu_state_vfat_t *st=cu_vfat_get_state();
 st->sys[0x200U+cl*2U]=0xFF; st->sys[0x201U+cl*2U]=0xFF;
 st->sys[0x20200U+cl*2U]=0xFF; st->sys[0x20201U+cl*2U]=0xFF;
}
static void link_cluster(unsigned from, unsigned to)
{
 cu_state_vfat_t *st=cu_vfat_get_state();
 st->sys[0x200U+from*2U]=to&255; st->sys[0x201U+from*2U]=(to>>8)&255;
 st->sys[0x20200U+from*2U]=to&255; st->sys[0x20201U+from*2U]=(to>>8)&255;
}

int main(void)
{
 char templ[]="/tmp/cuzebox-nested-XXXXXX"; char *root=mkdtemp(templ);
 char sub[1024], deep[1024], file[1024], renamed[1024], out[1024], linkp[1024], setpath[1024];
 cu_state_vfat_t *st; unsigned rs, subcl, childcl, newcl, dircl; unsigned char sec[512], dat[512];
 auint ss; uint8 *blob;
 assert(root);
 snprintf(setpath,sizeof(setpath),"%s/",root); filesys_setpath(setpath,NULL,0); filesys_set_sd_allow_new_files(TRUE);

 /* Existing host tree is represented as real FAT16 subdirectories. */
 path_join(sub,sizeof(sub),root,"sub"); must_mkdir(sub);
 path_join(deep,sizeof(deep),sub,"deep"); must_mkdir(deep);
 path_join(file,sizeof(file),deep,"hello.bin"); must_file(file,"HELLO");
 cu_vfat_reset(); st=cu_vfat_get_state(); rs=root_slot("sub"); subcl=ent_start(&st->sys[ROOT_BASE+rs*32U]); assert(subcl>=2);
 read_sector(cl_pos(subcl),sec); assert(sec[0]=='.' && sec[32]=='.' && sec[33]=='.');
 /* Entry 2 is DEEP directory. */
 assert((sec[2U*32U+11U]&0x10U)!=0); childcl=ent_start(&sec[2U*32U]); assert(childcl>=2);
 read_sector(cl_pos(childcl),sec); assert(sec[2U*32U+11U]==0x20U); newcl=ent_start(&sec[2U*32U]);
 read_sector(cl_pos(newcl),dat); assert(memcmp(dat,"HELLO",5)==0);

 /* State image contains only sparse metadata, and restores nested sectors. */
 ss=cu_vfat_state_size(); assert(ss>sizeof(cu_state_vfat_t)); blob=(uint8*)malloc(ss); assert(blob); assert(cu_vfat_state_save(blob,ss));
 cu_vfat_update(); /* old fixed-state rebuild intentionally loses sparse dirs */
 assert(cu_vfat_state_load(blob,ss)); free(blob);
 read_sector(cl_pos(subcl),sec); assert((sec[2U*32U+11U]&0x10U)!=0);

 /* Nested path traversal protection checks every component. */
 snprintf(out,sizeof(out),"%s-outside",root); must_mkdir(out);
 path_join(linkp,sizeof(linkp),root,"redir"); assert(symlink(out,linkp)==0);
 assert(!filesys_open(FILESYS_CH_SD,"redir/escape.bin")); assert(filesys_write(FILESYS_CH_SD,(const uint8*)"X",1)==0); unlink(linkp); rmdir(out);
 assert(!filesys_open(FILESYS_CH_SD,"sub/../escape.bin"));

 /* Guest mkdir order: initialize a free directory cluster BEFORE linking its
 ** parent entry, then add the parent entry. Sparse backing preserves it. */
 dircl=find_free_cluster(); alloc_one(dircl); memset(sec,0,sizeof(sec));
 memset(&sec[0], ' ',11); sec[0]='.'; sec[11]=0x10; sec[26]=dircl&255; sec[27]=(dircl>>8)&255;
 memset(&sec[32], ' ',11); sec[32]='.'; sec[33]='.'; sec[43]=0x10; sec[58]=subcl&255; sec[59]=(subcl>>8)&255;
 write_sector(cl_pos(dircl),sec); /* unowned/pending at this point */
 read_sector(cl_pos(subcl),sec); make_ent(&sec[3U*32U],"NEWDIR","",0x10,dircl,0); write_sector(cl_pos(subcl),sec);
 path_join(file,sizeof(file),sub,"newdir"); assert(isdir(file));
 read_sector(cl_pos(dircl),sec); assert(sec[0]=='.' && sec[32]=='.' && sec[33]=='.');

 /* Create nested file, including data written before its directory entry. */
 newcl=find_free_cluster(); alloc_one(newcl); memset(dat,0,sizeof(dat)); memcpy(dat,"NESTED",6); write_sector(cl_pos(newcl),dat);
 read_sector(cl_pos(dircl),sec); make_ent(&sec[2U*32U],"NEW","BIN",0x20,newcl,6); write_sector(cl_pos(dircl),sec);
 path_join(file,sizeof(file),sub,"newdir/new.bin"); assert(fsize(file)==6); { FILE *f=fopen(file,"rb"); char b[7]={0}; assert(f); assert(fread(b,1,6,f)==6); fclose(f); assert(strcmp(b,"NESTED")==0); }

 /* Same-sector FAT rename expressed as create-new-entry + delete-old-entry. */
 read_sector(cl_pos(dircl),sec); make_ent(&sec[5U*32U],"MOVED","BIN",0x20,newcl,6); sec[2U*32U]=0xE5; write_sector(cl_pos(dircl),sec);
 path_join(file,sizeof(file),sub,"newdir/moved.bin"); assert(fsize(file)==6);
 { char oldp[1024]; path_join(oldp,sizeof(oldp),sub,"newdir/new.bin"); assert(fsize(oldp)<0); }

 /* A directory may grow beyond its first 32 KiB cluster. FAT-chain changes
 ** are followed dynamically; no full directory image is preallocated. */
 {
  unsigned extcl = find_free_cluster();
  unsigned extfile;
  alloc_one(extcl); link_cluster(subcl, extcl);
  extfile = find_free_cluster(); alloc_one(extfile);
  memset(dat, 0, sizeof(dat)); memcpy(dat, "SECOND", 6); write_sector(cl_pos(extfile), dat);
  memset(sec, 0, sizeof(sec)); make_ent(&sec[0], "SECOND", "BIN", 0x20, extfile, 6); write_sector(cl_pos(extcl), sec);
  path_join(file, sizeof(file), sub, "second.bin"); assert(fsize(file)==6);
 }

 /* A complete child entry may also exist before its directory is linked. */
 {
  unsigned pre_dir = find_free_cluster();
  unsigned pre_file;
  alloc_one(pre_dir);
  pre_file = find_free_cluster();
  alloc_one(pre_file);
  memset(dat, 0, sizeof(dat)); memcpy(dat, "PREBUILT", 8); write_sector(cl_pos(pre_file), dat);
  memset(sec, 0, sizeof(sec));
  memset(&sec[0], ' ', 11); sec[0]='.'; sec[11]=0x10; sec[26]=pre_dir&255; sec[27]=(pre_dir>>8)&255;
  memset(&sec[32], ' ', 11); sec[32]='.'; sec[33]='.'; sec[43]=0x10; sec[58]=subcl&255; sec[59]=(subcl>>8)&255;
  make_ent(&sec[2U*32U], "PRE", "BIN", 0x20, pre_file, 8);
  write_sector(cl_pos(pre_dir), sec);
  read_sector(cl_pos(subcl), sec); make_ent(&sec[4U*32U], "PREMADE", "", 0x10, pre_dir, 0); write_sector(cl_pos(subcl), sec);
  path_join(file, sizeof(file), sub, "premade/pre.bin");
  assert(fsize(file) == 8);
  { FILE *f=fopen(file,"rb"); char b[9]={0}; assert(f); assert(fread(b,1,8,f)==8); fclose(f); assert(strcmp(b,"PREBUILT")==0); }
 }

 /* Rename the top directory in place; descendant mappings follow it. */
 st=cu_vfat_get_state(); fat83(&st->sys[ROOT_BASE+rs*32U],"RENAMED",""); { unsigned last = ((ROOT_BASE + rs*32U) & ~0x1FFU) | 0x1FFU; cu_vfat_write(last, st->sys[last]); }
 path_join(renamed,sizeof(renamed),root,"renamed"); assert(isdir(renamed)); assert(!isdir(sub));
 path_join(file,sizeof(file),renamed,"newdir/moved.bin"); assert(fsize(file)==6);
 memset(dat,0,sizeof(dat)); memcpy(dat,"CHANGED",7); write_sector(cl_pos(newcl),dat); filesys_flushall();
 { FILE *f=fopen(file,"rb"); char b[8]={0}; assert(f); assert(fread(b,1,7,f)==7); fclose(f); assert(strcmp(b,"CHANGED")==0); }

 /* Delete nested file, then its now-empty directory. */
 read_sector(cl_pos(dircl),sec); sec[5U*32U]=0xE5; write_sector(cl_pos(dircl),sec); assert(fsize(file)<0);
 /* NEWDIR still contains only . and .. after deleting NEW.BIN. */
 read_sector(cl_pos(subcl),sec); sec[3U*32U]=0xE5; write_sector(cl_pos(subcl),sec);
 path_join(file,sizeof(file),renamed,"newdir"); assert(!isdir(file));

 filesys_flushall();
 printf("nested_regression: PASS\n");
 return 0;
}
