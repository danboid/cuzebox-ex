/* SPDX-License-Identifier: MIT
 * Optional CUzeboxEX loading-speed hints for AVR-GCC games.
 * Hardware executes only RJMP (2 cycles); the four data words are skipped.
 */
#ifndef CUZEBOX_SPEED_H
#define CUZEBOX_SPEED_H
#if defined(__AVR__)
static __inline__ __attribute__((__always_inline__)) void cuzebox_speed_fast(void)
{
 __asm__ __volatile__("rjmp 1f\n\t.word 0x4355, 0x4558, 1, 0xfffe\n1:" ::: "memory");
}
static __inline__ __attribute__((__always_inline__)) void cuzebox_speed_normal(void)
{
 __asm__ __volatile__("rjmp 1f\n\t.word 0x4355, 0x4558, 0, 0xffff\n1:" ::: "memory");
}
#else
static inline void cuzebox_speed_fast(void) {}
static inline void cuzebox_speed_normal(void) {}
#endif
#endif
