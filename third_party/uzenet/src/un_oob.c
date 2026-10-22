#include "un_client.h"
#include <string.h>
static uint8_t seat_mask(const UrClient *c,unsigned i){
    return c->peers[i].known?c->peers[i].mask:
        (c->pace.peer[i].mask?c->pace.peer[i].mask:
         (c->oob.peer[i].mask?c->oob.peer[i].mask:c->service.peer[i].mask));
}
static uint8_t pending(const UrOob *o){
    uint8_t mask=0;
    for(unsigned i=0;i<4;i++)if(o->peer[i].rx_used &&
        o->peer[i].rx_count==o->peer[i].rx_total)mask|=1u<<i;
    return mask;
}
uint8_t uo_pending(const UrClient *c){return pending(&c->oob);}
uint8_t uo_internal_pending(const UrClient *c){return pending(&c->service);}
static uint8_t write_lane(UrClient *c,UrOob *o,const uint8_t *p,uint8_t n,uint32_t now){
    if(c->state!=UC_READY || n<7 || n>24)return 0;
    uint16_t id=(uint16_t)p[0]|((uint16_t)p[1]<<8);
    unsigned count=n-6,offset=p[5];uint8_t peers=0,covered=0;
    if(!id || !p[2] || (p[2]&~c->history.all) || (p[2]&c->history.local) ||
       !p[4] || p[4]>UO_MESSAGE_MAX || offset+count>p[4])return 0;
    if(id==o->id){
        if(p[2]!=o->dest || p[3]!=o->channel || p[4]!=o->total)return 0;
        if(offset<o->uploaded)return offset+count<=o->uploaded && !memcmp(o->data+offset,p+6,count);
        if(offset!=o->uploaded || o->uploaded==o->total)return 0;
    }else{
        if(o->id==65535 || id!=o->id+1 || offset)return 0;
        if(o->pending || o->uploaded!=o->total)return 2;
        for(unsigned i=0;i<4;i++)if(i!=c->peer){
            uint8_t mask=seat_mask(c,i);
            if(mask&p[2]){peers|=1u<<i;covered|=mask;
                if(o->peer[i].tx_id==UINT32_MAX){uc_fail(c,9);return 0;}}
        }
        if((covered&p[2])!=p[2])return 2; /* membership still arriving */
        o->id=id;o->dest=p[2];o->channel=p[3];o->total=p[4];o->uploaded=0;o->pending=peers;
        for(unsigned i=0;i<4;i++)if(peers&(1u<<i)){
            UoPeer *v=&o->peer[i];v->tx_id++;v->tx_offset=v->tx_sent=0;
            v->retry_ms=100;v->send_at=now;
        }
    }
    memcpy(o->data+offset,p+6,count);o->uploaded+=count;return 1;
}
uint8_t uo_write(UrClient *c,const uint8_t *p,uint8_t n,uint32_t now){
    return write_lane(c,&c->oob,p,n,now);
}
uint8_t uo_internal_send(UrClient *c,const uint8_t *data,uint8_t n,uint32_t now){
    UrOob *o=&c->service;
    if(!n || n>UO_MESSAGE_MAX || o->id==65535)return 0;
    if(o->pending || o->uploaded!=o->total)return 2;
    uint8_t dest=c->history.all&~c->history.local;
    if(!dest)return 1;
    uint16_t id=o->id+1;uint8_t p[24]={id,id>>8,dest,0,n,0};
    for(unsigned offset=0;offset<n;){
        unsigned count=n-offset;if(count>UO_WRITE_MAX)count=UO_WRITE_MAX;
        p[5]=offset;memcpy(p+6,data+offset,count);
        uint8_t result=write_lane(c,o,p,count+6,now);
        if(result!=1)return result;
        offset+=count;
    }
    return 1;
}
uint8_t uo_read(UrClient *c,const uint8_t p[3],uint8_t reply[24]){
    unsigned peer=p[0];uint8_t pending=uo_pending(c);
    if(peer>4 || !p[2] || p[2]>UO_READ_MAX)return 0;
    if(peer==4){
        if(p[1])return 0;
        if(c->oob.selected)peer=c->oob.selected-1;
        else for(unsigned k=0;k<4;k++){
            unsigned i=(c->oob.read_scan+k)&3;
            if(pending&(1u<<i)){peer=i;c->oob.selected=i+1;break;}
        }
    }
    if(peer==4 || !(pending&(1u<<peer))){reply[0]=4;return 1;}
    const UoPeer *v=&c->oob.peer[peer];
    if(p[1]>=v->rx_total)return 0;
    unsigned n=v->rx_total-p[1];if(n>p[2])n=p[2];
    reply[0]=peer;ur_put32(reply+1,v->rx_id);reply[5]=v->mask;
    reply[6]=v->rx_dest;reply[7]=v->rx_channel;reply[8]=v->rx_total;reply[9]=p[1];
    memcpy(reply+10,v->data+p[1],n);return 10+n;
}
static int ack_lane(UrOob *o,unsigned peer,uint32_t id){
    if(peer>=4 || !id)return 0;
    UoPeer *v=&o->peer[peer];
    if(id==v->consumed)return 1;
    if(!v->rx_used || id!=v->rx_id || v->rx_count!=v->rx_total)return 0;
    v->consumed=id;v->rx_used=0;o->read_scan=(peer+1)&3;
    if(o->selected==peer+1)o->selected=0;
    return 1;
}
int uo_ack(UrClient *c,unsigned peer,uint32_t id){return ack_lane(&c->oob,peer,id);}
int uo_internal_ack(UrClient *c,unsigned peer,uint32_t id){return ack_lane(&c->service,peer,id);}
static void acknowledge(UoPeer *v,uint32_t id,const uint8_t *p,uint8_t count){
    v->ack_pending=1;v->ack_id=id;memset(v->ack,0,8);
    v->ack[0]=p[0];v->ack[1]=p[1];v->ack[2]=count;v->ack[4]=p[4];v->ack[5]=p[5];
}
int uo_receive(UrClient *c,unsigned peer,uint8_t mask,int ack,uint32_t id,const uint8_t *p,uint32_t now){
    UrOob *o=p[5]?&c->service:&c->oob;
    if(c->state!=UC_READY || peer>=4 || peer==c->peer || !id || !mask ||
       (mask&~c->history.all) || (mask&c->history.local) ||
       !p[1] || p[1]>UO_MESSAGE_MAX || p[5]>1 || p[6] || p[7])return 0;
    UoPeer *v=&o->peer[peer];uint8_t known=seat_mask(c,peer);
    if((known && known!=mask) || (v->mask && v->mask!=mask))return 0;
    for(unsigned i=0;i<4;i++)if(i!=peer && (seat_mask(c,i)&mask))return 0;
    if(ack){
        if(p[3] || !(o->pending&(1u<<peer)) || o->uploaded!=o->total || id!=v->tx_id ||
           p[0]!=o->channel || p[1]!=o->total || p[4]!=(o->dest&mask) ||
           (p[2]!=v->tx_offset && p[2]!=v->tx_sent))return 0;
        if(p[2]>v->tx_offset){v->tx_offset=p[2];v->retry_ms=100;v->send_at=now-100;}
        if(v->tx_offset==o->total)o->pending&=(uint8_t)~(1u<<peer);
    }else{
        unsigned end=(unsigned)p[2]+p[3];
        if(!p[3] || p[3]>UO_FRAGMENT || end>p[1] || !p[4] || (p[4]&~c->history.local))return 0;
        for(unsigned i=p[3];i<UO_FRAGMENT;i++)if(p[8+i])return 0;
        if(id==v->rx_id){
            if(p[0]!=v->rx_channel || p[1]!=v->rx_total || p[4]!=v->rx_dest)return 0;
            if(p[2]<v->rx_count && (end>v->rx_count || memcmp(v->data+p[2],p+8,p[3])))return 0;
        }else{
            if(v->rx_id==UINT32_MAX || id!=v->rx_id+1)return 0;
            if(v->rx_used || p[2]){acknowledge(v,id,p,0);v->mask=mask;return 1;}
            v->rx_id=id;v->rx_total=p[1];v->rx_channel=p[0];v->rx_dest=p[4];v->rx_count=0;v->rx_used=1;
        }
        if(v->rx_used && p[2]==v->rx_count){memcpy(v->data+p[2],p+8,p[3]);v->rx_count=end;}
        acknowledge(v,id,p,v->rx_count);
    }
    v->mask=mask;return 1;
}
static int next_lane(UrClient *c,UrOob *o,uint32_t now,uint8_t packet[UR_PACKET_MAX],uint8_t destination[6]){
    if(c->state!=UC_READY || now-c->oob.last_tx<UO_INTERVAL_MS)return 0;
    /* The caller only schedules OOB after gameplay/control. One packet per
     * 20 ms across all destinations, and exponential retry backoff per peer. */
    for(unsigned k=0;k<4;k++){
        unsigned peer=(o->scan+k)&3;UoPeer *v=&o->peer[peer];int ack=v->ack_pending;
        if(!ack && (!(o->pending&(1u<<peer)) || o->uploaded!=o->total ||
            (v->tx_sent && now-v->send_at<v->retry_ms)))continue;
        uint8_t payload[32]={0},width=c->history.width;uint32_t id;
        if(ack){memcpy(payload,v->ack,8);id=v->ack_id;v->ack_pending=0;}
        else{
            unsigned n=o->total-v->tx_offset;if(n>UO_FRAGMENT)n=UO_FRAGMENT;
            payload[0]=o->channel;payload[1]=o->total;payload[2]=v->tx_offset;payload[3]=n;
            payload[4]=o->dest&seat_mask(c,peer);payload[5]=o==&c->service;
            memcpy(payload+8,o->data+v->tx_offset,n);
            id=v->tx_id;width|=0xc0;
            if(v->tx_sent>v->tx_offset && v->retry_ms<2000){v->retry_ms*=2;if(v->retry_ms>2000)v->retry_ms=2000;}
            v->tx_sent=v->tx_offset+n;v->send_at=now;
        }
        c->oob.last_tx=now;o->scan=(peer+1)&3;
        if(ud_mask(c,now)&(1u<<peer)){
            UoPeer *wire=&c->oob.peer[peer];
            if(wire->seq==UINT32_MAX){uc_fail(c,9);return 0;}
            memcpy(destination,c->peers[peer].address,6);
            return ur_seal(packet,c->peers[peer].key,8+c->peer,c->match,c->peer,
                ack?UD_OOB_ACK:UD_OOB,++wire->seq,id,c->history.local,width,payload);
        }
        if(c->oob.seq==UINT32_MAX){uc_fail(c,9);return 0;}
        memcpy(destination,c->endpoint,6);
        return ur_seal(packet,c->key,6,c->match,c->peer,ack?UR_OOB_ACK:UR_OOB,
            ++c->oob.seq,id,1u<<peer,width,payload);
    }
    return 0;
}
int uo_next(UrClient *c,uint32_t now,uint8_t packet[UR_PACKET_MAX],uint8_t destination[6]){
    for(unsigned k=0;k<2;k++){
        unsigned lane=(c->message_turn+k)&1;
        if(next_lane(c,lane?&c->service:&c->oob,now,packet,destination)){
            c->message_turn=lane^1;return 1;
        }
    }
    return 0;
}
