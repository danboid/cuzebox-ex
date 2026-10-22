/* Custom ESP8266 UART-v4 firmware adapter. Stock profiles remain inert. */
#ifndef CU_ESP_UZENET_H
#define CU_ESP_UZENET_H
#include "types.h"
#ifdef ENABLE_UZENET
void cu_uzenet_set_config_path(const char *path);
boole cu_uzenet_enabled(void);
void cu_uzenet_reset(void);
void cu_uzenet_tick(auint cycle);
auint cu_uzenet_pina(auint value, auint cycle);
boole cu_uzenet_uart_write(uint8 byte, auint cycle);
boole cu_uzenet_stock_write(const char *bytes, auint length);
auint cu_uzenet_stock_capacity(void);
#else
static inline void cu_uzenet_set_config_path(const char *path){(void)path;}
static inline boole cu_uzenet_enabled(void){return FALSE;}
static inline void cu_uzenet_reset(void){}
static inline void cu_uzenet_tick(auint cycle){(void)cycle;}
static inline auint cu_uzenet_pina(auint value,auint cycle){(void)cycle;return value;}
static inline boole cu_uzenet_uart_write(uint8 byte,auint cycle){(void)byte;(void)cycle;return FALSE;}
static inline boole cu_uzenet_stock_write(const char *bytes,auint length){(void)bytes;(void)length;return FALSE;}
static inline auint cu_uzenet_stock_capacity(void){return 4096;}
#endif
#endif
