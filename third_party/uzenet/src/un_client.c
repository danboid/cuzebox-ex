#include "un_client.h"
#include <string.h>
void uc_rtt(UrLink *link,uint32_t elapsed) {
    uint16_t value=elapsed>65535?65535:(uint16_t)elapsed;
    if(!link->samples){link->rtt=value;link->jitter=0;}
    else{
        int32_t delta=(int32_t)value-link->rtt;
        link->rtt+=delta/8;
        if(delta<0)delta=-delta;
        link->jitter+=(delta-link->jitter)/4;
    }
    if(link->samples<65535)link->samples++;
}
void uc_reset(UrClient *c) {volatile uint8_t *p=(volatile uint8_t*)c;size_t n=sizeof(*c);while(n--)*p++=0;}
void uc_fail(UrClient *c,uint8_t error) {
    /* Retain status for the AVR, erase the traffic secret immediately. */
    volatile uint8_t *p=c->key;unsigned n=sizeof(c->key);while(n--)*p++=0;
    for(unsigned i=0;i<4;i++){p=c->peers[i].key;n=32;while(n--)*p++=0;c->peers[i].known=0;}
    c->state=UC_FAILED;c->error=error;
}
int uc_begin(UrClient *c,const uint8_t cfg[19],const uint8_t nonce[4],uint32_t now,uint8_t request[25]) {
    if(c->state!=UC_IDLE || !cfg[16] || cfg[16]>4 || cfg[17]<2 || cfg[17]>4 || cfg[16]>cfg[17] ||
        ((cfg[18]&~UR_REDUNDANT)!=1 && (cfg[18]&~UR_REDUNDANT)!=2))return 0;
    uc_reset(c);memcpy(c->config,cfg,19);memcpy(c->nonce,nonce,4);c->state=UC_TICKET;c->born=now;
    request[0]=UR_OPEN;request[1]=(cfg[18]&UR_REDUNDANT)?UR_PROFILE_REDUNDANT:UR_PROFILE;memcpy(request+2,cfg,19);memcpy(request+21,nonce,4);return 1;
}
int uc_ticket(UrClient *c,const uint8_t t[68],uint32_t now) {
    unsigned bits=0,i;
    if(c->state!=UC_TICKET || memcmp(t,"UNCT",4) || t[4]<UR_PROFILE || t[4]>UR_PROFILE_REDUNDANT || memcmp(t+6,c->nonce,4))return 0;
    if(t[5]){uc_fail(c,t[5]);return 1;}
    for(i=0;i<4;i++)bits+=(t[23]>>i)&1;
    if((t[4]==UR_PROFILE_REDUNDANT)!=!!(c->config[18]&UR_REDUNDANT) || t[22]>3 || bits!=c->config[16] ||
       t[24]!=((1u<<c->config[17])-1) || t[25]!=(c->config[18]&~UR_REDUNDANT) ||
       !ur_history_init(&c->history,t[24],t[23],t[25]) || !ur_get32(t+26) || !(t[30]|t[31]) ||
       !ur_get32(t+64) || ur_get32(t+64)>3600){uc_fail(c,6);return 1;}
    memcpy(c->match,t+10,8);memcpy(c->seed,t+18,4);memcpy(c->endpoint,t+26,6);memcpy(c->key,t+32,32);
    c->peer=t[22];c->protocol=t[4];c->lease_ms=ur_get32(t+64)*1000;c->born=c->last_rx=now;c->last_tx=now-250;
    c->state=UC_BINDING;return 1;
}
int uc_receive(UrClient *c,const uint8_t *p,size_t n,uint32_t now) {
    uint32_t frame;
    if(c->state<UC_BINDING || c->state>UC_READY || n<48 || (n>48 && !UR_IS_OOB(p[4]) && c->protocol!=UR_PROFILE_REDUNDANT) ||
       p[5]!=c->peer || (p[23]&63)!=c->history.width ||
       !ur_open(p,n,c->key,UR_IS_OOB(p[4])?7:1,c->match,UR_IS_OOB(p[4])?&c->oob.replay:&c->replay))return 0;
    frame=ur_get32(p+18);c->last_rx=now;
    switch(p[4]){
    case UR_OOB_FROM:case UR_OOB_ACK_FROM:
        return uo_receive(c,p[22]>>4,p[22]&15,p[4]==UR_OOB_ACK_FROM,frame,p+24,now);
    case UR_PACE_FROM:case UR_PACE_REPLY_FROM:
        return up_receive(c,p[22]>>4,p[22]&15,p[4]==UR_PACE_REPLY_FROM,frame,p+24,now);
    case UR_CHALLENGE:
        memcpy(c->challenge,p+24,8);c->challenged=1;c->challenge_at=now;c->last_tx=now-250;break;
    case UR_BOUND:if(c->state==UC_BINDING)c->state=UC_WAITING;break;
    case UR_READY:c->state=UC_READY;break;
    case UR_FRAME:
        if((p[22]&c->history.local)==c->history.local){
            UrFrame *f=ur_frame(&c->history,frame,0);
            if(f && f->pending && f->transmitted==1)uc_rtt(&c->relay,now-f->sent);
        }
        if(p[22]>15 || !ur_merge_packet(&c->history,p))uc_fail(c,7);
        else if(frame>=c->history.floor && frame-c->history.floor<UR_WINDOW){
            if(frame>=c->relay.next)c->relay.next=frame+1;
            for(unsigned i=0;i<4;i++)if(c->peers[i].known && (p[22]&c->peers[i].mask)==c->peers[i].mask &&
                frame>=c->peers[i].link.next)c->peers[i].link.next=frame+1;
        }
        break;
    case UR_RELEASED:if(frame<=c->history.floor && frame>c->acked)c->acked=frame;break;
    case UR_ERROR:if(p[24]==2)uc_fail(c,7);break;
    case UR_END:uc_fail(c,8);break;
    default:return 0;
    }
    return 1;
}
int uc_submit(UrClient *c,uint32_t frame,const uint8_t pads[8]) {
    if(c->state!=UC_READY || frame<c->released || !ur_submit(&c->history,frame,pads))return 0;
    /* The game's own actual input is known immediately. Relay retention ACK
     * remains separate so direct play never waits on a server round trip. */
    ur_frame(&c->history,frame,0)->mask|=c->history.local;
    if(frame>=c->next)c->next=frame+1;
    return 1;
}
UrFrame *uc_fetch(UrClient *c,uint32_t frame) {
    UrFrame *f;
    if(c->state!=UC_READY || frame<c->released || !(f=ur_frame(&c->history,frame,1)))return 0;
    if(!c->wanted || c->want_frame!=frame){c->want_frame=frame;c->wanted=1;c->fetch_at=0;}
    return f;
}
int uc_release(UrClient *c,uint32_t next) {
    if(c->state!=UC_READY || next<c->released || next-c->history.floor>UR_WINDOW)return 0;
    for(uint32_t n=c->released;n<next;n++){
        UrFrame *f=ur_frame(&c->history,n,0);if(!f || f->mask!=c->history.all)return 0;
    }
    c->released=next;return 1;
}
static int uc_packet(UrClient *c,uint8_t p[48],uint8_t type,uint32_t frame,uint8_t mask,uint8_t width,const uint8_t pads[8],uint32_t now) {
    if(c->seq==UINT32_MAX){uc_fail(c,9);return 0;}
    c->last_tx=now;
    return ur_seal(p,c->key,0,c->match,c->peer,type,++c->seq,frame,mask,width,pads);
}
int uc_next(UrClient *c,uint32_t now,uint8_t p[48]) {
    unsigned i,pass;
    if(c->state==UC_TICKET){if(now-c->born>10000)uc_fail(c,10);return 0;}
    if(c->state<UC_BINDING || c->state>UC_READY)return 0;
    if(now-c->last_rx>15000 || now-c->born>=c->lease_ms){uc_fail(c,11);return 0;}
    if(now-c->last_tx<1)return 0;
    if(c->state>=UC_WAITING && now-c->last_rx>=1000){
        c->state=UC_BINDING;c->challenged=0;c->last_tx=now-250;
    }
    if(c->state==UC_BINDING){
        if(now-c->last_tx<250)return 0;
        if(c->challenged && now-c->challenge_at>1500)c->challenged=0;
        return uc_packet(c,p,c->challenged?UR_CONFIRM:UR_HELLO,0,0,c->history.width,c->challenged?c->challenge:0,now);
    }
    if(c->state==UC_READY){
        /* Released game inputs remain in the outbox until the relay has its
         * recovery copy. ACK loss must never discard an unsent input. */
        while(c->history.floor<c->released){
            UrFrame *f=ur_frame(&c->history,c->history.floor,0);
            if(!f || f->pending)break;
            c->history.floor++;
        }
        /* Fresh samples before recovery/control traffic. At 60 Hz there is
         * ample space between samples; the 64-frame window also bounds a
         * burst, so an application cannot overwrite unacknowledged input. */
        for(pass=0;pass<2;pass++){
        if(pass && now-c->last_tx<5)return 0;
        if(pass && c->history.floor>c->acked && now-c->release_at>=40){
            c->release_at=now;return uc_packet(c,p,UR_RELEASE,c->history.floor,0,c->history.width,0,now);
        }
        if(pass && c->wanted && now-c->fetch_at>=40){
            UrFrame *f=ur_frame(&c->history,c->want_frame,0);
            c->fetch_at=now;
            if(f && f->mask!=c->history.all)return uc_packet(c,p,UR_FETCH,c->want_frame,0,c->history.width,0,now);
        }
        for(i=0;i<UR_WINDOW;i++){
            /* In redundant mode drain fresh input newest-first, so a UART
             * burst becomes one current packet instead of stale singles. */
            unsigned index=(c->scan+((!pass && c->protocol==UR_PROFILE_REDUNDANT)?UR_WINDOW-1-i:i))&(UR_WINDOW-1);
            UrFrame *f=&c->history.frames[index];uint8_t pads[32];
            if(!f->valid || !f->pending || f->frame<c->history.floor || f->frame-c->history.floor>=UR_WINDOW ||
               (pass==0?f->transmitted:(f->transmitted && now-f->sent<25)))continue;
            uint8_t width=ur_samples(&c->history,f->frame,c->history.local,pads,c->protocol==UR_PROFILE_REDUNDANT);
            if(f->transmitted && c->retries<65535)c->retries++;
            for(unsigned n=0;n<=(width>>6);n++){
                UrFrame *sent=&c->history.frames[(index-n)&(UR_WINDOW-1)];sent->sent=now;
                if(sent->transmitted<255)sent->transmitted++;
            }
            c->scan=(uint8_t)((index+1)&(UR_WINDOW-1));
            uint8_t direct=ud_mask(c,now);
            return uc_packet(c,p,direct?UR_INPUT_DIRECT:UR_INPUT,f->frame,
                             c->history.local|(direct<<4),width,pads,now);
        }
        }
    }
    return now-c->last_tx>=250?uc_packet(c,p,UR_PING,0,0,c->history.width,0,now):0;
}
