#include "un_endpoint.h"
#include <string.h>
__attribute__((weak)) uint8_t ue_start(const uint8_t config[19],uint32_t now){(void)config;(void)now;return UE_NOT_READY;}
void ue_reset(UnEndpoint *e){memset(e,0,sizeof(*e));e->read_peer=4;e->policy[3]=17;}
static void error(UnEndpoint *e,uint8_t code){e->reply[1]=UE_ERROR;e->reply[2]=code;e->reply_n=3;}
static int ending(UnEndpoint *e,UrClient *c,uint32_t now){
    if(!c->services.end_code)return 0;
    uint8_t code=us_end_status(c,now);
    if(code)error(e,code);
    return 1;
}
void ue_tick(UnEndpoint *e,UrClient *c,uint32_t now){
    if(!e->configured || c->state!=UC_READY)return;
    if(!c->services.active){
        us_begin(c,e->policy,e->delay,e->window,now);
        up_begin(c,c->released,now);
    }
    us_tick(c,now);
}
static void ready(UnEndpoint *e,UrGame *g,UrClient *c,uint32_t now){
    if(c->state==UC_FAILED){error(e,UE_BACKEND);return;}
    if(c->state!=UC_READY)return;
    ue_tick(e,c,now);
    if(ending(e,c,now))return;
    if(c->services.failure){error(e,c->services.failure);return;}
    if(!c->services.ready)return;
    if(!e->running){
        e->delay=c->services.delay;
        ug_reset(g);g->active=1;g->base=c->released;
        e->frame=c->released;e->running=1;
        uint8_t neutral[8]={0};
        for(unsigned i=0;i<e->delay;i++)if(!uc_submit(c,e->frame+i,neutral)){error(e,UE_BACKEND);return;}
    }
    e->reply[1]=UE_OK;e->reply[2]=c->history.local;e->reply[3]=c->history.all;
    e->reply[4]=c->history.width;memcpy(e->reply+5,c->seed,4);e->reply_n=9;
}
static void pads_at(UrClient *c,uint32_t f,uint8_t pads[8]){
    UrFrame *v=ur_frame(&c->history,f,0);
    if(v)for(unsigned s=0;s<4;s++)if(v->mask&(1u<<s))memcpy(pads+2*s,v->pads+2*s,2);
}
static void repair_notice(UnEndpoint *e){
    e->reply[1]=UE_REPAIR;e->reply[2]=(uint8_t)e->repair_first;
    e->reply[3]=(uint8_t)(e->repair_end-e->repair_first);e->reply_n=4;
}
static void reconcile(UnEndpoint *e,UrGame *g,UrClient *c,uint8_t pads[8]){
    memcpy(pads,e->baseline,8);
    uint32_t release=c->released;
    for(uint32_t f=c->released;f<e->frame;f++){
        pads_at(c,f,pads);uint8_t *old=e->used[f&(UE_PREDICTION_SLOTS-1)];
        if(!e->repair && memcmp(pads,old,8)){
            e->repair=1;e->repair_first=e->repair_cursor=f;e->repair_end=e->frame;
        }
        if(e->repair)memcpy(old,pads,8); /* immutable replay snapshot */
        else{
            UrFrame *v=ur_frame(&c->history,f,0);
            if(release==f && v && v->mask==c->history.all){
                release=f+1;memcpy(e->baseline,pads,8);g->delivered[f&(UR_WINDOW-1)]=0;
            }
        }
    }
    (void)uc_release(c,release);
    pads_at(c,e->frame,pads);
}
static void replay(UnEndpoint *e,UrClient *c){
    e->reply[1]=UE_OK;e->reply[2]=(uint8_t)e->repair_cursor;e->reply_n=3;
    const uint8_t *pads=e->used[e->repair_cursor&(UE_PREDICTION_SLOTS-1)];
    for(unsigned s=0;s<4;s++)if(c->history.all&(1u<<s)){
        memcpy(e->reply+e->reply_n,pads+2*s,c->history.width);e->reply_n+=c->history.width;
    }
}
static void exchange(UnEndpoint *e,UrGame *g,UrClient *c,const uint8_t *p,uint8_t n,uint32_t now){
    unsigned local=__builtin_popcount(c->history.local)*c->history.width;
    if(!e->running || c->state!=UC_READY){error(e,UE_NOT_READY);return;}
    if(n!=local){error(e,UE_BAD_REQUEST);return;}
    if(c->services.failure || c->services.desync){error(e,c->services.failure?c->services.failure:UE_DESYNC);return;}
    if(ending(e,c,now) || c->services.barrier_wait)return;
    if(e->repair){repair_notice(e);return;}
    /* Only a NEW EXCHANGE after RUN acknowledges a completed simulation step.
     * Chat/status transactions never advance the game. */
    if(e->granted){
        if(e->frame>=0xffffffbfUL){error(e,UE_EXHAUSTED);return;}
        e->frame++;up_advance(c,e->frame,now);e->granted=e->sampled=0;
    }
    uint8_t predicted[8];reconcile(e,g,c,predicted);
    if(e->repair){repair_notice(e);return;}
    if(!e->sampled){
        if(e->frame>=0xffffffc0UL-e->delay){error(e,UE_EXHAUSTED);return;}
        uint8_t pads[8]={0};unsigned at=0;
        for(unsigned s=0;s<4;s++)if(c->history.local&(1u<<s)){
            memcpy(pads+2*s,p+at,c->history.width);at+=c->history.width;
        }
        /* Full recovery retention applies even if the game has consumed the
         * frame. Wait for Room acknowledgement rather than overwrite a slot. */
        if(e->frame+e->delay-c->history.floor>=UR_WINDOW)return;
        if(!uc_submit(c,e->frame+e->delay,pads)){error(e,UE_BACKEND);return;}
        e->sampled=1;
    }
    /* Once sampled, pads are immutable even if the user's buttons change
     * during WAIT. Echo all actual pads on RUN, including latched local ones. */
    UrFrame *v=uc_fetch(c,e->frame);
    e->reply[1]=uo_pending(c)?UE_OOB:0;
    if(!v)return;
    pads_at(c,e->frame,predicted);
    /* A newer all-actual frame cannot bypass an older unresolved gap and
     * exceed the game's snapshot window. */
    if((e->frame>c->released || v->mask!=c->history.all) && e->frame-c->released>=e->window)return;
    if(!up_run(c,now))return;
    e->granted=1;e->reply[1]|=UE_OK;
    memcpy(e->used[e->frame&(UE_PREDICTION_SLOTS-1)],predicted,8);
    for(unsigned s=0;s<4;s++)if(c->history.all&(1u<<s)){
        memcpy(e->reply+e->reply_n,predicted+2*s,c->history.width);e->reply_n+=c->history.width;
    }
}
static void chat_read(UnEndpoint *e,UrClient *c){
    uint8_t in[3]={e->read_peer,e->read_offset,14},out[24];
    unsigned n=uo_read(c,in,out);
    if(!n){error(e,UE_BACKEND);return;}
    if(out[0]==4){e->read_peer=4;e->read_count=0;return;}
    e->read_peer=out[0];e->read_id=ur_get32(out+1);e->read_count=n-10;
    e->reply[1]=UE_OK;
    /* source seats, destination seats, channel, total, offset, data */
    memcpy(e->reply+2,out+5,n-5);e->reply_n=n-3;
}
static void fence(UnEndpoint *e,UrClient *c,uint8_t op,const uint8_t *p,uint8_t n,uint32_t now){
    if(!e->running || c->state!=UC_READY){error(e,UE_NOT_READY);return;}
    if(op==UE_END && !n){
        us_end(c,US_ENDED,now);
        if(ending(e,c,now) && e->reply[1]==UE_ERROR && e->reply[2]==UE_ENDED){
            e->reply[1]=UE_OK;e->reply_n=2;
        }
        return;
    }
    if(op!=UE_BARRIER || n!=1){error(e,UE_BAD_REQUEST);return;}
    if(ending(e,c,now))return;
    if(c->services.failure || c->services.desync){error(e,c->services.failure?c->services.failure:UE_DESYNC);return;}
    /* Barriers never acknowledge a RUN. Rollback callers must first settle
     * predictions: every completed step must already be actual and confirmed. */
    if(e->repair || c->released!=e->frame){error(e,UE_BAD_REQUEST);return;}
    if(e->granted){
        UrFrame *v=uc_fetch(c,e->frame);
        if(!v || v->mask!=c->history.all || memcmp(v->pads,e->used[e->frame&(UE_PREDICTION_SLOTS-1)],8)){
            error(e,UE_BAD_REQUEST);return;
        }
    }
    int result=us_barrier(c,p[0],e->frame+e->granted,now);
    if(!result)error(e,UE_BAD_REQUEST);
    else if(result==2){c->services.barrier_wait=0;e->reply[1]=UE_OK;}
}
uint8_t ue_service(UnEndpoint *e,UrGame *g,UrClient *c,uint32_t now){
    ue_tick(e,c,now);
    if(!e->awaiting)return 0;
    e->reply[0]=e->tag;e->reply[1]=0;e->reply_n=2;
    if(e->request[1]==UE_EXCHANGE)
        exchange(e,g,c,e->request+2,e->request_n-2,now);
    else if(e->request[1]==UE_BARRIER || e->request[1]==UE_END)
        fence(e,c,e->request[1],e->request+2,e->request_n-2,now);
    else ready(e,g,c,now);
    if(!(e->reply[1]&(UE_OK|UE_ERROR|UE_REPAIR)))return 0;
    e->awaiting=0;un_send(UE_REPLY,e->reply,e->reply_n);return 1;
}
void ue_command(UnEndpoint *e,UrGame *g,UrClient *c,const uint8_t *p,uint8_t n,uint32_t now){
    uint8_t fail[3]={n?p[0]:0,UE_ERROR,UE_BAD_SEQUENCE};
    if(n<2 || n>24){fail[2]=UE_BAD_REQUEST;un_send(UE_REPLY,fail,3);return;}
    if(e->valid && p[0]==e->tag){
        if(n==e->request_n && !memcmp(p,e->request,n)){
            if(!e->awaiting)un_send(UE_REPLY,e->reply,e->reply_n);
        }
        else un_send(UE_REPLY,fail,3);
        return;
    }
    if(e->awaiting || p[0]!=(uint8_t)(e->tag+1)){un_send(UE_REPLY,fail,3);return;}
    memcpy(e->request,p,n);e->request_n=n;e->valid=1;e->tag=p[0];
    e->reply[0]=e->tag;e->reply[1]=0;e->reply_n=2;
    uint8_t op=p[1];p+=2;n-=2;
    if(op==UE_START && n==21 && (p[19]&0x7f)<=8 && p[20]<=8 &&
       (!(p[19]&0x80) || e->policy[1]<=(p[19]&0x7f)) && !e->configured){
        uint8_t config[19];memcpy(config,p,sizeof config);config[18]&=(uint8_t)~UE_DEFER;
        uint8_t result=ue_start(config,now);
        if(result)error(e,result);else{
            e->defer=!!(p[18]&UE_DEFER);e->configured=1;e->read_peer=4;e->delay=p[19]&0x7f;e->window=p[20];
            e->policy[0]=(p[19]&0x80)?US_AUTO_DELAY:0;e->policy[2]=e->delay;ready(e,g,c,now);
        }
    }else if(op==UE_POLL && !n && e->configured)ready(e,g,c,now);
    else if(op==UE_EXCHANGE)exchange(e,g,c,p,n,now);
    else if(op==UE_BARRIER || op==UE_END)fence(e,c,op,p,n,now);
    else if((op==UE_REPLAY_READ || op==UE_REPLAY_NEXT) && !n && e->repair && c->state==UC_READY){
        if(op==UE_REPLAY_NEXT && e->repair!=2)error(e,UE_BAD_REQUEST);
        else if(op==UE_REPLAY_NEXT && ++e->repair_cursor==e->repair_end){e->repair=0;e->reply[1]=UE_OK;}
        else{replay(e,c);e->repair=2;}
    }
    else if(op==UE_CREDIT && n==1 && e->running && c->state==UC_READY && (unsigned)g->credit+p[0]<=255){
        g->mode=UG_PUSH;g->credit+=p[0];e->reply[1]=UE_OK;e->reply[2]=g->credit;e->reply_n=3;
    }else if(op>=UE_CHAT_BEGIN && op<=UE_CHAT_STATUS){
        if(!e->running || c->state!=UC_READY)error(e,UE_NOT_READY);
        else if(op==UE_CHAT_BEGIN && n==3 && p[0] && !(p[0]&~c->history.all) &&
                !(p[0]&c->history.local) && p[2] && p[2]<=128){
            if(e->chat_writing || c->oob.pending || c->oob.uploaded!=c->oob.total){}
            else if(c->oob.id==65535)error(e,UE_EXHAUSTED);
            else{e->chat_dest=p[0];e->chat_channel=p[1];e->chat_total=p[2];e->chat_writing=1;e->reply[1]=UE_OK;}
        }else if(op==UE_CHAT_WRITE && n && n<=18 && e->chat_writing){
            uint8_t in[24];unsigned offset=e->chat_writing==1?0:c->oob.uploaded;
            unsigned id=c->oob.id+(e->chat_writing==1);
            in[0]=id;in[1]=id>>8;in[2]=e->chat_dest;in[3]=e->chat_channel;in[4]=e->chat_total;in[5]=offset;
            memcpy(in+6,p,n);uint8_t result=uo_write(c,in,n+6,now);
            if(!result)error(e,UE_BAD_REQUEST);
            else if(result==1){e->chat_writing=c->oob.uploaded==c->oob.total?0:2;e->reply[1]=UE_OK;}
        }else if(op==UE_CHAT_READ && !n)chat_read(e,c);
        else if(op==UE_CHAT_NEXT && !n && e->read_count){
            /* Only NEXT acknowledges a displayed/stored chunk. Merely reading
             * another game reply cannot consume chat. Cached retries are safe. */
            unsigned next=e->read_offset+e->read_count;
            if(next==c->oob.peer[e->read_peer].rx_total){
                if(!uo_ack(c,e->read_peer,e->read_id))error(e,UE_BACKEND);
                else{e->read_peer=4;e->read_offset=e->read_count=0;e->reply[1]=UE_OK;}
            }else{e->read_offset=next;e->read_count=0;chat_read(e,c);}
        }else if(op==UE_CHAT_STATUS && !n){
            e->reply[1]=UE_OK|(uo_pending(c)?UE_OOB:0);e->reply[2]=e->chat_writing?1:c->oob.pending?2:0;e->reply_n=3;
        }else error(e,UE_BAD_REQUEST);
    }else if(op==UE_SERVICE_INFO && !n){
        /* Version, feature flags, max name bytes, checksum slots, max delay.
         * Feature bits: status, automatic delay, names, checksums, dual lane. */
        static const uint8_t info[]={3,127,US_NAME_MAX,US_CHECK_SLOTS,8};
        e->reply[1]=UE_OK;memcpy(e->reply+2,info,sizeof info);e->reply_n=2+sizeof info;
    }else if(op==UE_DELAY_POLICY && n==2 && !e->configured && p[0]<=8 && p[1]>=10 && p[1]<=50){
        e->policy[1]=p[0];e->policy[3]=p[1];e->reply[1]=UE_OK;
    }else if(op==UE_NET_STATUS && !n){
        ue_tick(e,c,now);e->reply[1]=UE_OK;
        e->reply_n=2+us_status(c,e->frame,e->running?e->delay:c->services.delay,now,e->reply+2);
    }else if(op==UE_PEER_STATUS && n==1){
        uint8_t count=us_peer_status(c,p[0],now,e->reply+2);
        if(!count)error(e,UE_BAD_REQUEST);else{e->reply[1]=UE_OK;e->reply_n=2+count;}
    }else if(op==UE_PROFILE_SET && !e->configured){
        int result=us_profile_set(c,p,n);
        if(result!=1)error(e,result<0?UE_BACKEND:UE_BAD_REQUEST);else e->reply[1]=UE_OK;
    }else if(op==UE_PROFILE_GET && !e->configured && n==3 && p[0]<4 && p[2] && p[2]<=16){
        if(!p[1]){
            int length=us_profile_read(p[0],e->name_read);
            e->name_valid=length>=-1 && length<=US_NAME_MAX;
            e->name_length=length<0?0:length;e->name_seat=p[0];
        }
        if(!e->name_valid || e->name_seat!=p[0] || p[1]>e->name_length)error(e,UE_BACKEND);
        else{
            unsigned count=e->name_length-p[1];if(count>p[2])count=p[2];
            e->reply[1]=UE_OK;e->reply[2]=p[0];e->reply[3]=e->name_length;e->reply[4]=p[1];
            memcpy(e->reply+5,e->name_read+p[1],count);e->reply_n=5+count;
        }
    }else if(op>=UE_NAME_SET && op<=UE_CHECK_STATUS){
        if(!e->running || c->state!=UC_READY)error(e,UE_NOT_READY);
        else if(op==UE_NAME_SET){
            if(!us_name_set(c,p,n))error(e,UE_BAD_REQUEST);else e->reply[1]=UE_OK;
        }else if(op==UE_NAME_GET && n==3 && p[0]<4 && (c->history.all&(1u<<p[0])) && p[2] && p[2]<=16){
            unsigned seat=p[0];
            if(!p[1]){
                e->name_valid=!!(c->services.known&(1u<<seat));e->name_seat=seat;
                e->name_length=c->services.length[seat];
                memcpy(e->name_read,c->services.names[seat],US_NAME_MAX);
            }
            if(!e->name_valid && !p[1]){} /* unknown remote name: WAIT */
            else if(!e->name_valid || e->name_seat!=seat || p[1]>e->name_length)error(e,UE_BAD_REQUEST);
            else{
                unsigned count=e->name_length-p[1];if(count>p[2])count=p[2];
                e->reply[1]=UE_OK;e->reply[2]=seat;e->reply[3]=e->name_length;e->reply[4]=p[1];
                memcpy(e->reply+5,e->name_read+p[1],count);e->reply_n=5+count;
            }
        }else if(op==UE_CHECK && (n==8 || (n==4 && !e->window && e->granted))){
            /* Compact lockstep form refers to the current RUN grant. Metadata
             * never acknowledges/advances it; retries are transaction-cached. */
            uint32_t frame=n==4?e->frame:ur_get32(p);
            if((!e->granted && !e->frame) || frame>=e->frame+e->granted ||
               e->frame-frame>UR_WINDOW || (e->window && frame>=c->released))error(e,UE_BAD_REQUEST);
            else{
                int result=us_check(c,frame,ur_get32(p+n-4));
                if(!result)error(e,UE_BAD_REQUEST);else if(result==1)e->reply[1]=UE_OK;
            }
        }else if(op==UE_CHECK_STATUS && !n){
            UrServices *s=&c->services;e->reply[1]=UE_OK;
            e->reply[2]=s->check_known|(s->desync?2:0);ur_put32(e->reply+3,s->check_frame);
            ur_put32(e->reply+7,s->check_hash);e->reply[11]=0;e->reply[12]=s->desync;
            for(unsigned i=0;i<US_CHECK_SLOTS;i++)if(s->check[i].mask && s->check[i].frame==s->check_frame)e->reply[11]=s->check[i].mask;
            e->reply_n=13;
        }else error(e,UE_BAD_REQUEST);
    }else error(e,UE_BAD_REQUEST);
    if(e->defer && (op==UE_START || op==UE_POLL || op==UE_EXCHANGE || op==UE_BARRIER || op==UE_END) &&
       !(e->reply[1]&(UE_OK|UE_ERROR|UE_REPAIR)))e->awaiting=1;
    else un_send(UE_REPLY,e->reply,e->reply_n);
}
