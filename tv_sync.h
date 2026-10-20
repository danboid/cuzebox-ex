/* Optional horizontal TV sync approximation, in AVR clock units.
 * Display-only: never changes the emulated machine or its saved state.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef TV_SYNC_H
#define TV_SYNC_H
#include <stdint.h>

typedef struct {
 uint32_t edge;
 int phase;                    /* Predicted scan origin minus sync, Q8 clocks */
 int period;                   /* Half-line period, Q8 clocks */
 int frequency_error;          /* Fractional frequency correction remainder */
 int locked;
} tv_sync_t;

static inline void tv_sync_reset(tv_sync_t *s)
{
 s->edge = 0; s->phase = 0; s->period = 910 * 256; s->frequency_error = 0; s->locked = 0;
}

/* Both normal HSync and the half-line equalizing/serration edges participate.
 * A phase step settles over several lines; frequency follows more slowly.
 * Large gaps reacquire instead of letting reset/wrap/disconnect overflow math.
 */
static inline unsigned tv_sync_recovery_clamp(unsigned percent)
{
 return percent < 25 ? 25 : (percent > 800 ? 800 : percent);
}

/* 100% preserves the original response; 800% locks at each edge. */
static inline int tv_sync_edge(tv_sync_t *s, uint32_t cycle, unsigned recovery)
{
 uint32_t dt = cycle - s->edge;
 unsigned slots;
 unsigned gain = tv_sync_recovery_clamp(recovery) * 32 / 100;
 int error, divisor, correction;
 s->edge = cycle;
 if (!s->locked || dt < 455 || dt > 4096){
  s->phase = 0; s->period = 910 * 256; s->frequency_error = 0; s->locked = 1;
  return 0;
 }
 slots = (dt + 455) / 910;
 error = s->phase + (int)slots * s->period - (int)dt * 256;
 s->phase = error - (error * (int)gain) / 256;
 /* Scale frequency correction quadratically to preserve loop damping. */
 divisor = 262144 / (int)(gain * gain);
 s->frequency_error += error / (int)slots;
 correction = s->frequency_error / divisor;
 s->frequency_error -= correction * divisor;
 s->period -= correction;
 if (s->period < 906 * 256) s->period = 906 * 256;
 if (s->period > 914 * 256) s->period = 914 * 256;
 /* Bounded acquisition range also keeps both renderer sampling windows
  * within their 2032-clock source row, including the classic filter halo. */
 if (s->phase < -240 * 256) s->phase = -240 * 256;
 if (s->phase >  240 * 256) s->phase =  240 * 256;
 return s->phase / 256;
}
/* Presentation amplitude only: never feeds back into the PLL. */
static inline int tv_sync_scale(int offset, unsigned strength)
{
 if (strength > 100) strength = 100;
 return offset * (int)strength / 100;
}
#endif
