#include "filesys.h"
#include "cu_vfat.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ROOT_BASE 0x040200U

static void must_write_file(const char *path, const void *data, size_t len)
{
 FILE *f = fopen(path, "wb");
 assert(f != NULL);
 assert(fwrite(data, 1, len, f) == len);
 fclose(f);
}

static long long fsize(const char *path)
{
 struct stat st;
 if (stat(path, &st) != 0) return -1;
 return (long long)st.st_size;
}

static unsigned find_slot(const char *host)
{
 cu_state_vfat_t *st = cu_vfat_get_state();
 unsigned i;
 for (i = 0; i < CU_VFAT_ROOT_SIZE; ++i){
  if (strcmp(st->hnames[i], host) == 0) return i;
 }
 assert(!"slot not found");
 return 0;
}

static void fat83(unsigned char *d, const char *base, const char *ext)
{
 size_t i;
 memset(d, ' ', 11);
 for (i = 0; i < 8 && base[i]; ++i) d[i] = (unsigned char)base[i];
 for (i = 0; i < 3 && ext[i]; ++i) d[8+i] = (unsigned char)ext[i];
}

static void commit_root_sector(unsigned id)
{
 cu_state_vfat_t *st = cu_vfat_get_state();
 unsigned sector = (ROOT_BASE + id * 32U) & ~0x1FFU;
 unsigned last = sector + 0x1FFU;
 cu_vfat_write(last, st->sys[last]);
}

int main(void)
{
 char templ[] = "/tmp/cuzebox-sdtest-XXXXXX";
 char *dir = mkdtemp(templ);
 char path[1024], outside[1024], linkpath[1024], setpath[1024];
 unsigned char buf[32];
 unsigned id;
 cu_state_vfat_t *vs;
 FILE *f;

 assert(dir != NULL);
 snprintf(setpath, sizeof(setpath), "%s/", dir);
 filesys_setpath(setpath, NULL, 0);

 /* Existing-file writes remain allowed with the opt-in disabled. */
 snprintf(path, sizeof(path), "%s/existing.bin", dir);
 must_write_file(path, "abcdefgh", 8);
 filesys_set_sd_allow_new_files(FALSE);
 assert(filesys_open(FILESYS_CH_SD, "existing.bin"));
 filesys_setpos(FILESYS_CH_SD, 2);
 assert(filesys_write(FILESYS_CH_SD, (const uint8 *)"XY", 2) == 2);
 filesys_setpos(FILESYS_CH_SD, 8);
 assert(filesys_write(FILESYS_CH_SD, (const uint8 *)"Z", 1) == 0);
 filesys_flush(FILESYS_CH_SD);
 assert(fsize(path) == 8);

 /* Creation/growth disabled, enabled after persistent permission is set. */
 assert(!filesys_open(FILESYS_CH_SD, "new.bin"));
 assert(filesys_write(FILESYS_CH_SD, (const uint8 *)"N", 1) == 0);
 snprintf(path, sizeof(path), "%s/new.bin", dir);
 assert(fsize(path) < 0);
 filesys_set_sd_allow_new_files(TRUE);
 assert(!filesys_open(FILESYS_CH_SD, "new.bin"));
 assert(filesys_write(FILESYS_CH_SD, (const uint8 *)"NEW", 3) == 3);
 filesys_flush(FILESYS_CH_SD);
 assert(fsize(path) == 3);
 assert(filesys_resize_sd("new.bin", 1));
 assert(fsize(path) == 1);

 /* Path escape and host redirect leaves are rejected. */
 assert(!filesys_open(FILESYS_CH_SD, "../escape.bin"));
 snprintf(outside, sizeof(outside), "%s-outside", dir);
 must_write_file(outside, "SAFE", 4);
 snprintf(linkpath, sizeof(linkpath), "%s/link.bin", dir);
 assert(symlink(outside, linkpath) == 0);
 assert(!filesys_open(FILESYS_CH_SD, "link.bin"));
 assert(filesys_read(FILESYS_CH_SD, buf, 4) == 0);
 assert(filesys_write(FILESYS_CH_SD, (const uint8 *)"EVIL", 4) == 0);
 assert(!filesys_resize_sd("link.bin", 100));
 assert(fsize(outside) == 4);
 unlink(linkpath);

 /* FAT-level rename/delete/slot reuse tracks the correct host object. */
 snprintf(path, sizeof(path), "%s/old.txt", dir);
 must_write_file(path, "OLDDATA", 7);
 cu_vfat_reset();
 id = find_slot("old.txt");
 vs = cu_vfat_get_state();
 fat83(&vs->sys[ROOT_BASE + id * 32U], "RENAMED", "TXT");
 commit_root_sector(id);
 snprintf(path, sizeof(path), "%s/renamed.txt", dir);
 assert(fsize(path) == 7);
 snprintf(path, sizeof(path), "%s/old.txt", dir);
 assert(fsize(path) < 0);
 assert(strcmp(vs->hnames[id], "renamed.txt") == 0);

 vs->sys[ROOT_BASE + id * 32U] = 0xE5U;
 commit_root_sector(id);
 snprintf(path, sizeof(path), "%s/renamed.txt", dir);
 assert(fsize(path) < 0);
 assert(vs->hnames[id][0] == 0);

 memset(&vs->sys[ROOT_BASE + id * 32U], 0, 32);
 fat83(&vs->sys[ROOT_BASE + id * 32U], "REUSED", "BIN");
 vs->sys[ROOT_BASE + id * 32U + 11] = 0x20; /* archive, regular file */
 commit_root_sector(id);
 snprintf(path, sizeof(path), "%s/reused.bin", dir);
 assert(fsize(path) == 0);
 assert(strcmp(vs->hnames[id], "reused.bin") == 0);

 /* 4 GiB aggregate cap: use a sparse host file so the test is cheap. */
 snprintf(path, sizeof(path), "%s/filler.bin", dir);
 f = fopen(path, "wb");
 assert(f != NULL);
 assert(ftruncate(fileno(f), (off_t)((4ULL * 1024ULL * 1024ULL * 1024ULL) - 64ULL)) == 0);
 fclose(f);
 assert(!filesys_resize_sd("reused.bin", 128));
 assert(fsize(path) == (long long)((4ULL * 1024ULL * 1024ULL * 1024ULL) - 64ULL));
 /* Shrinking/deleting remain possible while at/over the cap. */
 assert(filesys_resize_sd("filler.bin", 16));
 assert(filesys_delete_sd("filler.bin"));
 assert(fsize(path) < 0);

 memset(buf, 0, sizeof(buf));
 filesys_flushall();
 printf("sd_write_regression: PASS\n");
 return 0;
}
