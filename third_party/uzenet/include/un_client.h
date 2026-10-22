#ifndef UN_CLIENT_H
#define UN_CLIENT_H
#include "un_relay.h"
#include "un_pace.h"
#include "un_oob.h"
#include "un_services.h"
enum {UC_IDLE,UC_TICKET,UC_BINDING,UC_WAITING,UC_READY,UC_FAILED};
typedef struct {uint32_t next;uint16_t rtt,jitter,samples;} UrLink;
typedef struct {
    UrReplay replay;
    uint8_t key[32],address[6],candidate[6],reply_address[6],mask,known,active,reply_pending;
    uint32_t seq,token,probe_at,send_at,last_ok,reply_token,generation;
    UrLink link;
} UrPeer;
typedef struct UrClient {
    UrHistory history;
    UrPace pace;
    UrOob oob;
    /* Same authenticated message codec; independent bounded consumption.
     * An unread game chat message cannot block ESP-owned metadata. */
    UrOob service;
    UrServices services;
    uint8_t message_turn;
    UrReplay replay;
    UrPeer peers[4];
    uint8_t key[32],match[8],config[19],nonce[4],seed[4],endpoint[6],challenge[8];
    uint8_t state,error,peer,challenged,wanted,scan,protocol;
    uint32_t seq,born,last_rx,last_tx,challenge_at,want_frame,fetch_at,release_at,acked,lease_ms;
    uint32_t released;
    uint32_t next;
    UrLink relay;
    uint16_t tx_errors,retries;
} UrClient;
void uc_reset(UrClient *c);
int uc_begin(UrClient *c,const uint8_t config[19],const uint8_t nonce[4],uint32_t now,uint8_t request[25]);
int uc_ticket(UrClient *c,const uint8_t ticket[68],uint32_t now);
int uc_receive(UrClient *c,const uint8_t *packet,size_t length,uint32_t now);
/* Call on UART/network activity or once per millisecond while active. Fresh
 * samples are limited to one datagram/ms, maintenance to one per 5 ms.
 * Returns one authenticated datagram, or zero.
 * All packet OUTPUT buffers below require UR_PACKET_MAX bytes. On success
 * transmit UR_SIZE(packet) bytes (48 without redundancy, up to 72 with it). */
int uc_next(UrClient *c,uint32_t now,uint8_t packet[48]);
int uc_submit(UrClient *c,uint32_t frame,const uint8_t pads[8]);
UrFrame *uc_fetch(UrClient *c,uint32_t frame);
int uc_release(UrClient *c,uint32_t next);
void uc_fail(UrClient *c,uint8_t error);
void uc_rtt(UrLink *link,uint32_t elapsed);
/* Transport-independent peer path: endpoints are IPv4[4], little-endian port16.
 * No key, endpoint, or Internet packet crosses the AVR interface. */
int ud_offer(UrClient *c,const uint8_t ticket[68],uint32_t now);
int ud_receive(UrClient *c,const uint8_t *packet,size_t length,const uint8_t source[6],uint32_t now);
uint8_t ud_mask(const UrClient *c,uint32_t now);
int ud_next(UrClient *c,unsigned peer,uint32_t now,uint8_t packet[48],uint8_t destination[6]);
int ud_input(UrClient *c,unsigned peer,const uint8_t relay[48],uint32_t now,uint8_t packet[48],uint8_t destination[6]);
#endif
