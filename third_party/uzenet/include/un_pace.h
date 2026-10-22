#ifndef UN_PACE_H
#define UN_PACE_H
#include <stdint.h>
/* Pacing is automatic. Progress counts completed simulation ticks, never
 * copied inputs or rollback replays. No timestamps/clocks cross the UART. */
#define UP_INTERVAL_MS 100
#define UP_STALE_MS 1000
#define UP_HARD_LEAD 24
#define UP_INPUT_LEAD 8
typedef struct {
    uint32_t frame,seen_at,token,sent_at,sent_frame,send_at,reply_token,reply_echo;
    int16_t lead; /* filtered signed frame advantage, Q8 */
    uint16_t rtt,jitter; /* progress probe RTT, including relayed paths */
    uint8_t mask,samples,retries,reply_pending;
} UpPeer;
typedef struct {
    UpPeer peer[4];
    uint32_t frame,born,advanced_at,correct_at,pause_at,serial,last_tx;
    uint16_t period; /* observed local simulation period, Q8 milliseconds */
    uint8_t active,granted,pause_ms,scan;
} UrPace;
struct UrClient;
void up_begin(struct UrClient *c,uint32_t frame,uint32_t now);
int up_valid(const struct UrClient *c,uint32_t frame);
void up_advance(struct UrClient *c,uint32_t frame,uint32_t now);
uint8_t up_run(struct UrClient *c,uint32_t now);
int up_receive(struct UrClient *c,unsigned peer,uint8_t mask,int response,
               uint32_t frame,const uint8_t payload[8],uint32_t now);
/* At most one authenticated, 48-byte progress packet per call; destination
 * chooses a verified direct peer or Room. No extra RTOS wakeups. */
int up_next(struct UrClient *c,uint32_t now,uint8_t packet[48],uint8_t destination[6]);
#endif
