/* SPDX-License-Identifier: MIT
 * Emulator hints. Each request is a two-cycle RJMP on hardware.
 */
#ifndef CUZEBOX_HEX_HINTS_H
#define CUZEBOX_HEX_HINTS_H
#define EMU_HINT_INTERLACE 0x01U
#define EMU_HINT_WEAVE 0x02U
#define EMU_HINT_AREA_WEIGHTED 0x04U
#define EMU_HINT_SPI_RAM 0x80U
#define EMU_HINT_ESP8266 0x81U
#define EMU_HINT_SPEED 0x82U
#define EMU_HINT_SD_TIMING 0x83U
#define EMU_HINT_UZELINK 0x84U
#define EMU_HINT_ESP8266_AP 0x85U
#define EMULATOR_HINT_WORD(command, value) \
 ((command) == EMU_HINT_SPEED ? (value) : ((command) | ((value) << 8)))
#if defined(__AVR__)
#define EMULATOR_HINT_MARKER(command, value) do { \
 __asm__ __volatile__("rjmp 1f\n\t.word 0x4355, %0, %1, %2\n1:" \
 : : "n" ((command) == EMU_HINT_SPEED ? 0x4558U : 0x4848U), \
     "n" (EMULATOR_HINT_WORD((command), (value))), \
     "n" (EMULATOR_HINT_WORD((command), (value)) ^ 0xffffU) : "memory"); \
} while (0)
#else
#define EMULATOR_HINT_MARKER(command, value) ((void)0)
#endif
/* Constant arguments: SPI RAM takes 64 KiB banks, SD timing takes 0..3,
 * and all other options take 0/1. Set peripherals before initializing them. */
#define EmulatorHint(option, value) do { \
 (void)sizeof(char[((option) == EMU_HINT_SPI_RAM ? \
   ((value) >= 0 && (value) <= 128 && (((value) & ((value)-1)) == 0)) : \
   (option) == EMU_HINT_SD_TIMING ? ((value) >= 0 && (value) <= 3) : \
   (((option) == EMU_HINT_INTERLACE || (option) == EMU_HINT_WEAVE || \
     (option) == EMU_HINT_AREA_WEIGHTED || (option) == EMU_HINT_ESP8266 || \
     (option) == EMU_HINT_SPEED || (option) == EMU_HINT_UZELINK || \
     (option) == EMU_HINT_ESP8266_AP) && ((value) == 0 || (value) == 1))) ? 1 : -1]); \
 EMULATOR_HINT_MARKER((option), ((option) >= EMU_HINT_SPI_RAM ? (value) : ((value) ? (option) : 0U))); \
} while (0)
#endif
