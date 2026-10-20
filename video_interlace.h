/* Optional NTSC field identification from the post-equalizing HSync phase.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef VIDEO_INTERLACE_H
#define VIDEO_INTERLACE_H
#include <stdint.h>
typedef struct {
 uint32_t edge;
 unsigned seen, current, completed, alternations, active;
 unsigned sync_phase, sync_count, short_entry;
} video_interlace_t;
static inline void video_interlace_reset(video_interlace_t *s)
{
 s->edge=0; s->seen=0; s->current=0; s->completed=0;
 s->alternations=0; s->active=0;
 s->sync_phase=0; s->sync_count=0; s->short_entry=0;
}
static inline void video_interlace_field(video_interlace_t *s,
                                          uint32_t cycle, unsigned parity)
{
 uint32_t elapsed=cycle-s->edge;
 s->completed=s->current;
 if(s->seen && parity!=s->current && elapsed>=1820U*260U && elapsed<=1820U*265U){
  if(s->alternations<3) s->alternations++;
 }else s->alternations=0;
 s->active=(s->alternations>=2);
 s->seen=1; s->edge=cycle; s->current=parity;
}
/* Validate the complete six equalizing / six serration / six equalizing
 * pulse train before accepting a field boundary. Short pulses also occur in
 * custom renderers and must never independently reset the raster counter.
 * Return 254 when a preamble is confirmed, or 270 at its end. */
static inline unsigned video_interlace_sync(video_interlace_t *s,
 unsigned width, unsigned interval, uint32_t cycle)
{
 unsigned eq=width>=48U && width<=88U;
 unsigned wide=width>=740U && width<=810U;
 unsigned half=interval>=876U && interval<=944U;
 if(s->sync_phase==1U){
  if(eq && half && s->sync_count<6U){
   s->sync_count++;
   return (s->sync_count==3U) ? 254U : 0U;
  }
  if(wide && s->sync_count==6U && interval>=1582U && interval<=1650U){
   s->sync_phase=2U;s->sync_count=1U;return 0U;
  }
 }else if(s->sync_phase==2U){
  if(wide && half && s->sync_count<6U){s->sync_count++;return 0U;}
  if(eq && s->sync_count==6U && interval>=170U && interval<=238U){
   s->sync_phase=3U;s->sync_count=1U;return 0U;
  }
 }else if(s->sync_phase==3U){
  if(eq && half && s->sync_count<6U){s->sync_count++;return 0U;}
  if(s->sync_count==6U && width>=116U && width<=156U &&
     ((interval>=944U && interval<=1012U) || (interval>=1854U && interval<=1922U))){
   video_interlace_field(s,cycle,interval>=1854U ? 0U : 1U);
   s->sync_phase=0U;return 270U;
  }
 }
 s->sync_phase=0U;s->sync_count=0U;
 if(eq && ((interval>=808U && interval<=876U) || (interval>=1718U && interval<=1786U))){
  s->sync_phase=1U;s->sync_count=1U;s->short_entry=(interval<1000U);
  /* Once locked, recognize the preamble at its expected vertical position.
   * This preserves the original half-line/audio grouping. Else acquire
   * conservatively from consecutive equalizing pulses. */
  if(s->seen && (uint32_t)(cycle-s->edge)>=1820U*248U &&
     (uint32_t)(cycle-s->edge)<=1820U*258U) return 252U;
 }
 return 0U;
}
#endif
