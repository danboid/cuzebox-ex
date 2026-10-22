#ifndef UN_SERVICES_H
#define UN_SERVICES_H
#include <stdint.h>
#define US_NAME_MAX 24
#define US_CHECK_SLOTS 4
#define US_AUTO_DELAY 1
#define US_BARRIER_MS 120000UL
#define US_END_MS 10000UL
enum { US_ENDED=9,US_SYNC_ERROR,US_SESSION_TIMEOUT };
typedef struct {uint32_t frame,hash;uint8_t mask,send;} UsCheck;
typedef struct {uint32_t frame;uint16_t generation;uint8_t label,mask;} UsBarrier;
typedef struct {
    uint8_t active,flags,minimum,maximum,tick,window,ready,delay,failure;
    uint8_t seen,offered,hello_send,offer_send,proposal,clamped;
    uint8_t policy[4][5],offers[4];
    uint32_t born;
    uint8_t names[4][US_NAME_MAX],length[4],known,dirty;
    uint8_t upload[US_NAME_MAX],upload_seat,upload_total,upload_used;
    UsCheck check[US_CHECK_SLOTS];
    uint8_t desync,check_known;
    uint32_t check_frame,check_hash;
    UsBarrier barrier[4];
    uint32_t barrier_at,end_at;
    uint8_t barrier_wait,barrier_send,end_code,end_mask,end_send,end_result;
} UrServices;
struct UrClient;
void us_begin(struct UrClient *c,const uint8_t policy[4],uint8_t delay,uint8_t window,uint32_t now);
void us_tick(struct UrClient *c,uint32_t now);
/* Session names use assigned global seats 0..3; only locally owned seats
 * may be written. args: seat, offset, total, bytes. Zero total clears. */
int us_name_set(struct UrClient *c,const uint8_t *args,uint8_t n);
/* Saved local-controller profiles, independent of assigned room seats.
 * Read: -1 missing, -2 storage failure, otherwise byte length. Write: 1 OK.
 * Platform hooks touch only the Uzenet name namespace, never AT credentials. */
int us_profile_read(uint8_t local,uint8_t text[US_NAME_MAX]);
int us_profile_write(uint8_t local,const uint8_t *text,uint8_t n);
int us_profile_set(struct UrClient *c,const uint8_t *args,uint8_t n);
/* 0 invalid, 1 accepted/idempotent, 2 slots busy. CRC is game-defined. */
int us_check(struct UrClient *c,uint32_t frame,uint32_t hash);
uint8_t us_status(struct UrClient *c,uint32_t frame,uint8_t delay,uint32_t now,uint8_t out[22]);
uint8_t us_peer_status(struct UrClient *c,uint8_t seat,uint32_t now,uint8_t out[22]);
/* 0 invalid, 1 waiting, 2 all seats reached this confirmed simulation boundary.
 * Generation lives on the ESP; UART retries never create another generation. */
int us_barrier(struct UrClient *c,uint8_t label,uint32_t frame,uint32_t now);
void us_end(struct UrClient *c,uint8_t code,uint32_t now);
/* Zero while converging; otherwise terminal reason (never resume this session). */
uint8_t us_end_status(struct UrClient *c,uint32_t now);
#endif
