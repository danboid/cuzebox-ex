#define SDL_MAIN_HANDLED
#undef main
#include "../../cu_vfat.h"
#define main storage_emulator_main
#include "../../main.c"
#undef main
#include "../../capture.c"
#include "../../savestate.c"
#include <time.h>

#define CHECK(c, message) do { \
 if (!(c)){ fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); return 1; } \
} while (0)

static uint32 reference_crc(uint32 value, uint8 const *data, size_t size)
{
 size_t i;
 unsigned bit;
 for (i = 0; i < size; ++i){
  value ^= data[i];
  for (bit = 0; bit < 8; ++bit){
   value = (value >> 1) ^ ((0U - (value & 1U)) & 0xEDB88320U);
  }
 }
 return value;
}

static int crc_test(boole benchmark)
{
 uint8 data[65536];
 uint32 random = 1234567U;
 unsigned i, length;
 CHECK(crc(NULL, 0) == 0U, "empty CRC");
 CHECK(crc((uint8 const*)"123456789", 9) == 0xCBF43926U, "IEEE CRC32 known vector");
 for (i = 0; i < sizeof(data); ++i){
  random = random * 1664525U + 1013904223U;
  data[i] = (uint8)(random >> 24);
 }
 for (length = 0; length <= sizeof(data); length = length < 64 ? length + 1 : length * 2){
  uint32 expected = reference_crc(random, data, length);
  uint32 first = crc_bytes(random, data, length / 2);
  CHECK(crc_bytes(random, data, length) == expected, "random payload/initial CRC matches bitwise reference");
  CHECK(crc_bytes(first, data + length / 2, length - length / 2) == expected, "incremental CRC matches");
 }
 if (benchmark){
  uint32 (*functions[])(uint32, uint8 const*, size_t) = { reference_crc, crc_bytes };
  unsigned trial, algorithm;
  volatile uint32 digest = 0;
  for (trial = 0; trial < 3; ++trial){
   for (algorithm = 0; algorithm < 2; ++algorithm){
    clock_t start = clock();
    for (i = 0; i < 512; ++i){ digest = functions[algorithm](digest, data, sizeof(data)); }
    printf("CRC %s, 32 MiB, trial %u: %.6f s (digest %08x)\n",
           algorithm ? "nibble" : "bitwise", trial + 1,
           (double)(clock() - start) / CLOCKS_PER_SEC, (unsigned)digest);
   }
  }
 }
 return 0;
}

static int chunks_test(void)
{
 uint8 payload[524], header[12], *buffer = NULL, *saved;
 uint32 size, capacity = 0;
 char type[4];
 FILE *file = fopen("chunks.bin", "w+b");
 CHECK(file, "temporary chunk file");
 memset(payload, 0xA5, sizeof(payload));
 capture_file = file; capture_failed = FALSE;
 CHECK(write_chunk("SECT", payload, sizeof(payload)), "write sector chunk");
 CHECK(write_chunk("LACH", payload, 28), "write smaller chunk");
 CHECK(write_chunk("TEST", NULL, 0), "write zero length chunk");
 rewind(file);
 CHECK(read_chunk(file, type, &buffer, &size, &capacity) && size == sizeof(payload), "read sector chunk");
 CHECK(!memcmp(buffer, payload, size), "chunk payload preserved");
 saved = buffer;
 CHECK(read_chunk(file, type, &buffer, &size, &capacity) && size == 28 && !memcmp(type, "LACH", 4), "read following latch");
 CHECK(buffer == saved && capacity == sizeof(payload), "reuse bounded buffer for smaller chunk");
 CHECK(read_chunk(file, type, &buffer, &size, &capacity) && size == 0, "empty chunk CRC");
 CHECK(!read_chunk(file, type, &buffer, &size, &capacity), "EOF rejected as a chunk");
 rewind(file);
 memcpy(header, "TEST", 4); put32(header + 4, MOVIE_MAX_CHUNK + 1U); put32(header + 8, 0);
 CHECK(fwrite(header, 1, sizeof(header), file) == sizeof(header), "oversized fixture");
 rewind(file);
 CHECK(!read_chunk(file, type, &buffer, &size, &capacity) && capacity == sizeof(payload), "oversize rejected before allocation");
 rewind(file); put32(header + 4, sizeof(payload)); put32(header + 8, crc(payload, sizeof(payload)) ^ 1U);
 CHECK(fwrite(header, 1, sizeof(header), file) == sizeof(header), "bad CRC fixture");
 rewind(file);
 CHECK(!read_chunk(file, type, &buffer, &size, &capacity), "bad CRC rejected with reused buffer");
 free(buffer); fclose(file); capture_file = NULL;
 CHECK(remove("chunks.bin") == 0, "remove chunk fixture");
 return 0;
}

static int states_test(void)
{
 savestate_blob_t blob = {0};
 savestate_hdr_t header, bad;
 savestate_spir_meta_t meta, bad_meta;
 size_t meta_offset;
 auint pc;
 cu_avr_reset(); cu_spir_set_size(2); cu_vfat_reset();
 CHECK(savestate_capture_to_mem(&blob) && savestate_validate_blob(&blob), "valid current snapshot");
 memcpy(&header, blob.data, sizeof(header));
 meta_offset = sizeof(header) + header.cpu_size;
 memcpy(&meta, blob.data + meta_offset, sizeof(meta));
 pc = cu_avr_get_state()->pc = 123U;
 bad = header; bad.vfat_size = 0xFFFFFFFFU;
 memcpy(blob.data, &bad, sizeof(bad));
 CHECK(!savestate_restore_from_mem(&blob) && cu_avr_get_state()->pc == pc, "overflowing block sum rejected before CPU mutation");
 memcpy(blob.data, &header, sizeof(header));
 bad_meta = meta; bad_meta.size = MAX_SPI_RAM_SIZE + 1U;
 memcpy(blob.data + meta_offset, &bad_meta, sizeof(bad_meta));
 CHECK(!savestate_restore_from_mem(&blob) && cu_avr_get_state()->pc == pc, "nested SPI size cannot overrun backing allocation");
 CHECK(cu_spir_get_state()->size == meta.size, "invalid state leaves SPI RAM unchanged");
 memcpy(blob.data + meta_offset, &meta, sizeof(meta));
 blob.data[blob.size - 1U] = 0xFF;
 CHECK(!savestate_validate_blob(&blob), "movie preflight validates timer trailer");
 blob.data[blob.size - 1U] = 0;
 CHECK(savestate_restore_from_mem(&blob), "current state restores");
 blob.size -= sizeof(savestate_timer1_t);
 CHECK(savestate_restore_from_mem(&blob), "legacy state without timer trailer restores");
 savestate_free_blob(&blob);
 cu_spir_set_size(0);
 CHECK(savestate_capture_to_mem(&blob) && savestate_validate_blob(&blob), "snapshot with disabled SPI RAM is valid");
 CHECK(savestate_restore_from_mem(&blob), "disabled SPI RAM restores");
 savestate_free_blob(&blob);
 return 0;
}

int main(int argc, char **argv)
{
 (void)argv;
 if (crc_test(argc > 1) || chunks_test() || states_test()) return 1;
 puts("Movie CRC/buffer and savestate validation regression passed.");
 return 0;
}
