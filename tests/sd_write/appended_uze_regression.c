#include "filesys.h"
#include "cu_vfat.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ROOT_BASE 0x040200U
#define CL_BYTES 0x8000U
#define ROM_SIZE (512U + 60000U)
#define FILE_SIZE (ROM_SIZE + 100000U)

static unsigned char pattern(unsigned pos) { return (unsigned char)((pos * 37U + (pos >> 8)) & 255U); }

int main(void)
{
 char temp[] = "/tmp/cuzebox-appended-XXXXXX";
 char path[1024];
 char* root = mkdtemp(temp);
 FILE* f;
 cu_state_vfat_t* st;
 unsigned slot, start, offset, i;
 assert(root);
 snprintf(path, sizeof(path), "%s/SHADGATE.UZE", root);
 f = fopen(path, "wb"); assert(f);
 for (i = 0; i < FILE_SIZE; ++i){ assert(fputc(pattern(i), f) != EOF); }
 assert(fclose(f) == 0);
 filesys_setpath(path, NULL, 0);
 cu_vfat_reset();
 st = cu_vfat_get_state();
 for (slot = 0; slot < CU_VFAT_ROOT_SIZE; ++slot){
  if (!strcmp(st->hnames[slot], "SHADGATE.UZE")){ break; }
 }
 assert(slot < CU_VFAT_ROOT_SIZE);
 {
  uint8 const* e = &st->sys[ROOT_BASE + slot * 32U];
  unsigned size = e[28] | ((unsigned)e[29] << 8) | ((unsigned)e[30] << 16) | ((unsigned)e[31] << 24);
  assert(!memcmp(e, "SHADGATEUZE", 11));
  assert(size == FILE_SIZE);
  start = e[26] | ((unsigned)e[27] << 8);
 }
 /* Read each sector, including appended data across FAT cluster boundaries.
 ** Periodic browser saves currently close the active channel; reads must reopen it. */
 for (offset = 0; offset < FILE_SIZE; offset += 512U){
  unsigned pos = CU_VFAT_SYS_SIZE + (start - 2U) * CL_BYTES + offset;
  unsigned count = FILE_SIZE - offset;
  if (count > 512U){ count = 512U; }
  if ((offset & 2047U) == 0U){ filesys_flushall(); }
  for (i = 0; i < count; ++i){ assert(cu_vfat_read(pos + i) == pattern(offset + i)); }
 }
 filesys_flushall();
 assert(unlink(path) == 0);
 assert(rmdir(root) == 0);
 puts("appended_uze_regression: PASS");
 return 0;
}
