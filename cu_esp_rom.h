/* ESP8266 ROM protocol and persistent simulated flash; no Xtensa execution. */
#ifndef CU_ESP_ROM_H
#define CU_ESP_ROM_H
#include "types.h"
#ifdef ENABLE_ESP_ROM
void cu_esp_rom_set_path(const char *config);
int cu_esp_rom_reset(int gpio0_low);
int cu_esp_rom_active(void);
int cu_esp_rom_blocked(void);
int cu_esp_rom_reboot_pending(void);
void cu_esp_rom_byte(uint8 byte);
void cu_esp_rom_output(const uint8 *bytes, unsigned length);
int cu_esp_rom_partition(const char *name,uint32 *offset,uint32 *size);
const uint8 *cu_esp_rom_read(uint32 offset,uint32 size);
int cu_esp_rom_erase(uint32 offset,uint32 size);
int cu_esp_rom_write_begin(uint32 offset,uint32 size);
int cu_esp_rom_write_byte(uint8 byte);
const uint8 *cu_esp_rom_pki(const char *name,unsigned slot,unsigned *size);
#else
static inline void cu_esp_rom_set_path(const char *p){(void)p;}
static inline int cu_esp_rom_reset(int low){(void)low;return 0;}
static inline int cu_esp_rom_active(void){return 0;}
static inline int cu_esp_rom_blocked(void){return 0;}
#endif
#endif
