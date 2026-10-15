#define SDL_MAIN_HANDLED
#undef main
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif
#define main rom_loading_emulator_main
#include "../../main.c"
#undef main

#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); return 1; } } while (0)

static void hex_record(FILE* f, unsigned address, uint8 a, uint8 b)
{
 unsigned sum = 2U + (address >> 8) + (address & 255U) + a + b;
 fprintf(f, ":02%04X00%02X%02X%02X\n", address, a, b, (-sum) & 255U);
}

int main(void)
{
 uint8 image[65536], uze[516], before[65536];
 cu_ufile_header_t head;
 boole is_uze, bootpri;
 FILE* f;
 auint i;
 cu_state_cpu_t* cpu = cu_avr_get_state();
 appcfg_defaults(&main_cfg);
 CHECK(main_cfg.resident_bootloader_enable, "resident bootloader defaults on");
 /* Isolate these fixtures from an optional user override or ROM dump. */
 main_cfg.resident_bootloader_file[0] = 0;
 filesys_setpath("game.hex", NULL, 0U);
 f = fopen("game.hex", "w");
 CHECK(f, "create full-flash game fixture");
 hex_record(f, 0U, 0x0CU, 0x94U);
 hex_record(f, 0xF000U, 0x34U, 0x12U);
 hex_record(f, 0xFFFEU, 0x78U, 0x56U);
 fputs(":00000001FF\n", f); fclose(f);
 f = fopen("small.hex", "w");
 CHECK(f, "create small HEX fixture");
 hex_record(f, 0U, 0x0CU, 0x94U);
 fputs(":00000001FF\n", f); fclose(f);
 f = fopen("boot.hex", "w");
 CHECK(f, "create boot-only fixture");
 hex_record(f, 0xF000U, 0x34U, 0x12U);
 fputs(":00000001FF\n", f); fclose(f);
 memset(image, 0, sizeof(image));
 image[0] = 0x0CU; image[1] = 0x94U;
 memset(&head, 0, sizeof(head));
 head.version = 1U; head.pmemsize = 4U;
 CHECK(cu_ufile_build_image(uze, sizeof(uze), image, &head) == sizeof(uze), "build UZE fixture");
 f = fopen("game.uze", "wb");
 CHECK(f, "create UZE fixture");
 CHECK(fwrite(uze, 1U, sizeof(uze), f) == sizeof(uze), "write UZE fixture"); fclose(f);

 CHECK(main_load_code_rom_file("game.uze", image, &head, &is_uze, &bootpri), "load resident UZE");
 CHECK(is_uze && bootpri, "UZE selects resident reset");
 CHECK(!memcmp(image + CU_BUILTIN_BOOTLOADER_ORIGIN, cu_builtin_bootloader, CU_BUILTIN_BOOTLOADER_SIZE), "UZE retains complete built-in bootloader");
 memcpy(cpu->crom, image, sizeof(image)); cu_avr_autofuse(bootpri); cu_avr_reset();
 CHECK(cpu->pc == 0x7800U, "UZE reset enters bootloader");
 CHECK(main_load_code_rom_file("game.hex", image, &head, &is_uze, &bootpri), "load HEX occupying boot section");
 CHECK(!is_uze && !bootpri && main_cfg.resident_bootloader_enable, "HEX overrides reset policy without changing preference");
 CHECK(image[0xF000U] == 0x34U && image[0xFFFFU] == 0x56U, "HEX retains data through final flash byte");
 CHECK(image[0xF002U] == 0xFFU && image[2] == 0xFFU, "HEX holes are erased, not resident code");
 memcpy(cpu->crom, image, sizeof(image)); cu_avr_autofuse(bootpri); cu_avr_reset();
 CHECK(cpu->pc == 0U, "large HEX reset enters application");
 cu_avr_reset(); CHECK(cpu->pc == 0U, "subsequent reset keeps HEX application vector");
 CHECK(main_load_code_rom_file("small.hex", image, &head, &is_uze, &bootpri), "load small HEX");
 for (i = CU_BUILTIN_BOOTLOADER_ORIGIN; i < sizeof(image); i++){
  CHECK(image[i] == 0xFFU, "small HEX erases entire boot section");
 }
 CHECK(main_load_code_rom_file("boot.hex", image, &head, &is_uze, &bootpri), "load boot-only HEX");
 CHECK(!is_uze && bootpri && image[0] == 0xFFU, "boot-only HEX reset policy");
 memcpy(cpu->crom, image, sizeof(image)); cu_avr_autofuse(bootpri); cu_avr_reset();
 CHECK(cpu->pc == 0x7800U, "boot-only HEX enters boot section");
 CHECK(main_load_code_rom_file("game.uze", image, &head, &is_uze, &bootpri), "return to UZE after HEX");
 CHECK(is_uze && bootpri && !memcmp(image + CU_BUILTIN_BOOTLOADER_ORIGIN, cu_builtin_bootloader, CU_BUILTIN_BOOTLOADER_SIZE), "UZE restores bootloader automatically");
#ifdef _WIN32
 _mkdir("nested");
#else
 mkdir("nested", 0700);
#endif
 f = fopen("nested/game.uze", "wb"); CHECK(f, "create nested game fixture");
 CHECK(fwrite(uze, 1U, sizeof(uze), f) == sizeof(uze), "write nested game"); fclose(f);
 strcpy(main_cfg.resident_bootloader_file, "boot.hex");
 CHECK(main_load_code_rom_file("nested/game.uze", image, &head, &is_uze, &bootpri), "load game beside a separate bootloader override directory");
 CHECK(is_uze && bootpri && image[0xF000U] == 0x34U, "override imported without losing game directory");
 CHECK(filesys_open(FILESYS_CH_EMU, "game.uze"), "game directory remains active after override"); filesys_flush(FILESYS_CH_EMU);
 main_cfg.resident_bootloader_file[0] = 0;
 memcpy(before, image, sizeof(image));
 f = fopen("bad.hex", "w"); CHECK(f, "create invalid fixture");
 hex_record(f, 0U, 0x34U, 0x12U); fputs("invalid\n", f); fclose(f);
 CHECK(!main_load_code_rom_file("bad.hex", image, &head, &is_uze, &bootpri), "reject invalid HEX");
 CHECK(!memcmp(image, before, sizeof(image)), "failed load preserves current flash");
 main_cfg.resident_bootloader_enable = FALSE;
 CHECK(main_load_code_rom_file("game.uze", image, &head, &is_uze, &bootpri) && !bootpri, "explicit resident-off preference still honored");
 /* Header byte 0x19A is a full-byte timing enum; unknown future values survive
 ** serialization but resolve to NORMAL when used by this emulator. */
 appcfg_defaults(&main_cfg);
 CHECK(main_cfg.sd_timing_preset == CU_SPISD_PRESET_UZE_HINT, "new configs follow UZE hints");
 head.sd_timing = CU_SPISD_PRESET_FAST; head.pmemsize = 4U;
 CHECK(cu_ufile_build_image(uze, sizeof(uze), image, &head) == sizeof(uze), "build timing-hint UZE");
 CHECK(uze[0x19A] == 3U && cu_ufile_load_buf(uze, sizeof(uze), before, &head), "parse byte 0x19A");
 main_rom_head = head; main_rom_is_uze = TRUE; main_sd_apply_policy();
 CHECK(cu_spisd_model_get()->init_ms == 50U && main_cfg.sd_timing_preset == 4U, "FAST hint applies without replacing selection policy");
 main_cfg_capture_runtime();
 CHECK(main_cfg.sd_timing_preset == 4U, "runtime capture retains HINT");
 CHECK(appcfg_save(&main_cfg, "hint.cfg"), "save HINT selection");
 appcfg_defaults(&main_cfg); CHECK(appcfg_load_or_create(&main_cfg, "hint.cfg") && main_cfg.sd_timing_preset == 4U, "HINT persists");
 main_cfg.sd_timing_preset = CU_SPISD_PRESET_SLOW; main_sd_apply_policy();
 CHECK(cu_spisd_model_get()->init_ms == 1000U, "manual timing overrides ROM hint");
 main_cfg.sd_timing_preset = CU_SPISD_PRESET_UZE_HINT;
 main_rom_head.sd_timing = 255U; main_sd_apply_policy();
 CHECK(cu_spisd_model_get()->init_ms == 500U, "unknown hint falls back to NORMAL");
 head.sd_timing = 255U; cu_ufile_build_image(uze, sizeof(uze), image, &head);
 CHECK(cu_ufile_load_buf(uze, sizeof(uze), before, &head) && head.sd_timing == 255U, "future whole-byte hints round trip");
 main_rom_head.sd_timing = 0U; main_sd_apply_policy();
 CHECK(cu_spisd_model_get()->init_ms == 500U, "unspecified hint falls back to NORMAL");
 main_rom_head.sd_timing = 3U; main_rom_is_uze = FALSE; main_sd_apply_policy();
 CHECK(cu_spisd_model_get()->init_ms == 500U, "HEX does not inherit previous UZE timing");
 puts("ROM loading regression: PASS (UZE/HEX switching, 64 KiB flash, reset vectors, failed loads)");
 return 0;
}
