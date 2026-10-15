#include "eepdump.h"
#include "filesys.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
 char temp[] = "/tmp/cuzebox-eeprom-XXXXXX";
 char path[1024];
 uint8 fresh[2048], saved[2048], loaded[2048];
 char* root = mkdtemp(temp);
 unsigned i;
 FILE* f;
 assert(root);
 snprintf(path, sizeof(path), "%s/game.uze", root);
 filesys_setpath(path, NULL, 0);
 eepdump_load(fresh);
 assert(fresh[0] == 0x5A && fresh[1] == 0x55);
 assert(fresh[2] == 1 && fresh[3] == 32 && fresh[4] == 1);
 assert(fresh[5] == 1 && fresh[7] == 0x38 && fresh[8] == 8);
 assert(fresh[0x16] == 1); /* GAME */
 for(i = 9; i < 32; ++i){ if(i != 0x16) assert(fresh[i] == 0); }
 for(i = 32; i < sizeof(fresh); ++i){ assert(fresh[i] == 255); }
 eepdump_save(fresh); eepdump_load(loaded);
 assert(!memcmp(loaded, fresh, sizeof(fresh)));
 /* Existing MENU choice and arbitrary game save bytes must survive intact. */
 for(i = 0; i < sizeof(saved); ++i){ saved[i] = (uint8)(i * 37U); }
 memcpy(saved, fresh, 32); saved[0x16] = 0;
 eepdump_save(saved); eepdump_load(loaded);
 assert(!memcmp(loaded, saved, sizeof(saved)));
 saved[0x16] = 1; eepdump_save(saved); eepdump_load(loaded);
 assert(!memcmp(loaded, saved, sizeof(saved)));
 /* A complete unformatted dump is also an existing user image: leave it alone. */
 memset(saved, 255, sizeof(saved)); eepdump_save(saved); eepdump_load(loaded);
 assert(!memcmp(loaded, saved, sizeof(saved)));
 /* Retain the previous fallback for incomplete files, using the new defaults. */
 snprintf(path, sizeof(path), "%s/eeprom.bin", root);
 f = fopen(path, "wb"); assert(f); assert(fwrite(saved, 1, 10, f) == 10); fclose(f);
 eepdump_load(loaded); assert(!memcmp(loaded, fresh, sizeof(fresh)));
 filesys_flushall(); assert(unlink(path) == 0); assert(rmdir(root) == 0);
 puts("EEPROM GAME default and existing-save preservation: PASS");
 return 0;
}
