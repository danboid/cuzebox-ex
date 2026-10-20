#include "cu_ufile.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#undef main
/* This test uses only memory images; MinGW retains unused file entry points. */
boole filesys_open(auint ch, char const *name) { (void)ch; (void)name; abort(); }
auint filesys_read(auint ch, uint8 *dest, auint len) { (void)ch; (void)dest; (void)len; abort(); }
void filesys_flush(auint ch) { (void)ch; abort(); }
#endif

int main(int argc, char **argv)
{
    uint8 image[66048], rebuilt[66048], code[65536];
    cu_ufile_header_t h;
    FILE *f;
    size_t n;
    assert(argc == 2);
    assert(PERIPHERAL_LIGHTGUN == 4 && PERIPHERAL_MULTITAP == 8);
    assert(PERIPHERAL_SPIRAM == 16 && PERIPHERAL_ESP8266 == 32);
    f = fopen(argv[1], "rb"); assert(f);
    n = fread(image, 1, sizeof(image), f); fclose(f);
    assert(cu_ufile_load_buf(image, (auint)n, code, &h));
    assert(h.psupport == image[0x152] && h.pdefault == image[0x193]);
    assert(h.uart == image[0x19B]);
    assert(cu_ufile_build_image(rebuilt, sizeof(rebuilt), code, &h) == n);
    assert(rebuilt[0x152] == image[0x152]);
    assert(rebuilt[0x193] == image[0x193]);
    assert(rebuilt[0x19B] == image[0x19B]);
    assert(!memcmp(rebuilt + 512, image + 512, n - 512));
    puts("UZE peripheral/UART round trip: PASS");
    return 0;
}
