#ifndef UN_GAME_H
#define UN_GAME_H
#include "un_client.h"
#include "un_wire.h"
#define UG_PUSH 1
typedef struct {
    uint32_t base;
    uint8_t delivered[UR_WINDOW];
    uint8_t active,mode,credit_id,last_grant,credit,scan;
} UrGame;
/* PLAY establishes a full-frame anchor and selects optional credit push.
 * All deliveries contain the same actual-input record, including partial
 * masks. A lockstep game waits for all seats; rollback can predict missing ones.
 * TICK uses frame suffixes only within
 * the retained 64-frame window. One outstanding request; retry only that
 * request, never retain retries across a reset or an entire 256-frame epoch. */
void ug_reset(UrGame *g);
uint8_t ug_command(UrGame *g,UrClient *c,uint8_t type,const uint8_t *p,uint8_t n,uint32_t now);
/* Call only with an empty reply slot and no command being received. */
void ug_push(UrGame *g,UrClient *c);
void ug_recover(UrGame *g,UrClient *c);
#endif
