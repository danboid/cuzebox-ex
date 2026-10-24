/* SPDX-License-Identifier: MIT
 * Emulator hints. Each request is a two-cycle RJMP on hardware.
 */
#ifndef CUZEBOX_HEX_HINTS_H
#define CUZEBOX_HEX_HINTS_H
#define EMU_HINT_INTERLACE 0x01U
#define EMU_HINT_WEAVE 0x02U
#define EMU_HINT_AREA_WEIGHTED 0x04U
#define EMU_HINT_SPI_RAM 0x80U
#if defined(__AVR__)
#define EMULATOR_HINT_MARKER(command, value) do { \
 __asm__ __volatile__("rjmp 1f\n\t.word 0x4355, 0x4848, %0, %1\n1:" \
 : : "n" ((command) | ((value) << 8)), \
     "n" (((command) | ((value) << 8)) ^ 0xffffU) : "memory"); \
} while (0)
#else
#define EMULATOR_HINT_MARKER(command, value) ((void)0)
#endif
/* Constant arguments: display options take 0/1; SPI RAM takes 64 KiB banks.
 * Request SPI RAM capacity before initializing or relying on the device. */
#define EmulatorHint(option, value) do { \
 (void)sizeof(char[((option) == EMU_HINT_SPI_RAM ? \
   ((value) >= 0 && (value) <= 128 && (((value) & ((value)-1)) == 0)) : \
   (((option) == EMU_HINT_INTERLACE || (option) == EMU_HINT_WEAVE || \
     (option) == EMU_HINT_AREA_WEIGHTED) && ((value) == 0 || (value) == 1))) ? 1 : -1]); \
 EMULATOR_HINT_MARKER((option), ((option) == EMU_HINT_SPI_RAM ? (value) : ((value) ? (option) : 0U))); \
} while (0)
#endif
