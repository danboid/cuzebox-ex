#include "cu_esp.h"
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>

static cu_state_esp_t test_state;
cu_state_esp_t* cu_esp_get_state(void){ return &test_state; }
auint cu_esp_get_time_seconds(void){ return (auint)time(NULL); }
void cu_message(char const* fmt,...){(void)fmt;} void cu_error(char const*fmt,...){(void)fmt;} void cu_unf(char const*s){(void)s;}
void cu_esp_txp(const char*s){(void)s;} void cu_esp_txl(const char*s,auint n){(void)s;(void)n;} void cu_esp_txi(sint32 v){(void)v;}
void cu_esp_lan_init(cu_state_esp_t *s){ (void)s; }
void cu_esp_lan_shutdown(cu_state_esp_t *s){ (void)s; }
void cu_esp_lan_tick(cu_state_esp_t *s, uint32 n){ (void)s; (void)n; }
uint8 cu_esp_lan_virtual_link_present(cu_state_esp_t *s, uint32 x){ (void)s; (void)x; return 0u; }
uint8 cu_esp_lan_virtual_link_open(cu_state_esp_t *s, uint32 x){ (void)s; (void)x; return 0u; }
sint32 cu_esp_lan_virtual_open(cu_state_esp_t *s, uint32 x, const char *h, uint32 p, uint32 t, uint32 lp, uint32 u){ (void)s;(void)x;(void)h;(void)p;(void)t;(void)lp;(void)u; return -1; }
sint32 cu_esp_lan_virtual_send(cu_state_esp_t *s, uint32 x, const uint8 *b, uint16 l){ (void)s;(void)x;(void)b;(void)l; return -1; }
sint32 cu_esp_lan_virtual_recv(cu_state_esp_t *s, uint32 x, uint8 *b, uint16 l){ (void)s;(void)x;(void)b;(void)l; return -1; }
void cu_esp_lan_virtual_close(cu_state_esp_t *s, uint32 x, uint8 n){ (void)s;(void)x;(void)n; }

struct broker_ctx { int ws; _Atomic unsigned port; _Atomic int ready; _Atomic int ok; _Atomic int stage; };

static int recv_exact(int s, void *vp, size_t n){
    unsigned char *p=(unsigned char*)vp;
    while(n){ ssize_t r=recv(s,p,n,0); if(r<=0)return -1; p+=(size_t)r; n-=(size_t)r; }
    return 0;
}
static int send_all(int s,const void *vp,size_t n){
    const unsigned char*p=(const unsigned char*)vp;
    while(n){ ssize_t r=send(s,p,n,0); if(r<=0)return -1; p+=(size_t)r;n-=(size_t)r; }
    return 0;
}
static int mqtt_packet_from_stream(int s,uint8_t*out,size_t cap,size_t *olen){
    uint8_t b; size_t n=0; unsigned mult=1,rem=0;
    if(recv_exact(s,&b,1))return -1; if(n>=cap)return -1;out[n++]=b;
    do{ if(recv_exact(s,&b,1))return -1;if(n>=cap)return -1;out[n++]=b;rem+=(unsigned)(b&127u)*mult;mult*=128u;if(mult>128u*128u*128u*128u)return -1;}while(b&0x80u);
    if(n+rem>cap)return -1;if(recv_exact(s,out+n,rem))return -1;n+=rem;*olen=n;return 0;
}
static int ws_read_frame(int s,uint8_t*out,size_t cap,size_t*olen){
    uint8_t h[2],mask[4];uint64_t len;int masked;
    if(recv_exact(s,h,2))return -1;len=h[1]&0x7fu;masked=(h[1]&0x80u)!=0;
    if(len==126u){uint8_t x[2];if(recv_exact(s,x,2))return -1;len=((uint64_t)x[0]<<8)|x[1];}
    else if(len==127u){uint8_t x[8];if(recv_exact(s,x,8))return -1;len=0;for(int i=0;i<8;i++)len=(len<<8)|x[i];}
    if(len>cap)return -1;if(masked&&recv_exact(s,mask,4))return -1;if(recv_exact(s,out,(size_t)len))return -1;
    if(masked)for(size_t i=0;i<(size_t)len;i++)out[i]^=mask[i&3u];*olen=(size_t)len;return 0;
}
static int ws_send_frame(int s,const uint8_t*p,size_t len){
    uint8_t h[10];size_t n=0;h[n++]=0x82u;if(len<126u)h[n++]=(uint8_t)len;else if(len<=65535u){h[n++]=126u;h[n++]=(uint8_t)(len>>8);h[n++]=(uint8_t)len;}else{return -1;}
    return send_all(s,h,n)||send_all(s,p,len)?-1:0;
}
static int broker_recv_packet(int s,int ws,uint8_t*out,size_t cap,size_t*olen){return ws?ws_read_frame(s,out,cap,olen):mqtt_packet_from_stream(s,out,cap,olen);}
static int broker_send_packet(int s,int ws,const uint8_t*p,size_t n){return ws?ws_send_frame(s,p,n):send_all(s,p,n);}
static int mqtt_find_topic_payload(const uint8_t*p,size_t n,const char *topic,const char *payload){
    if(n<4 || (p[0]&0xF0u)!=0x30u)return 0;size_t i=1;unsigned m=1,rem=0;uint8_t b;do{if(i>=n)return 0;b=p[i++];rem+=(b&127u)*m;m*=128u;}while(b&0x80u);if(i+rem>n||rem<2)return 0;unsigned tl=((unsigned)p[i]<<8)|p[i+1];i+=2;if(i+tl>n)return 0;if(strlen(topic)!=tl||memcmp(p+i,topic,tl))return 0;i+=tl;if(((p[0]>>1)&3u)!=0){if(i+2>n)return 0;i+=2;}return strlen(payload)==n-i&&!memcmp(p+i,payload,n-i);
}
static void *broker_thread(void *vp){
    struct broker_ctx*c=(struct broker_ctx*)vp;int ls=-1,cs=-1,one=1;struct sockaddr_in sa;socklen_t sl=sizeof(sa);uint8_t pkt[4096];size_t pn=0;
    ls=socket(AF_INET,SOCK_STREAM,0);if(ls<0)goto out;setsockopt(ls,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));memset(&sa,0,sizeof(sa));sa.sin_family=AF_INET;sa.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(bind(ls,(struct sockaddr*)&sa,sizeof(sa))||getsockname(ls,(struct sockaddr*)&sa,&sl)||listen(ls,1))goto out;atomic_store(&c->port,ntohs(sa.sin_port));atomic_store(&c->ready,1);cs=accept(ls,NULL,NULL);if(cs<0)goto out;
    {struct timeval tv={5,0};setsockopt(cs,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));setsockopt(cs,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));}
    if(c->ws){char req[4096];size_t rn=0;while(rn+1<sizeof(req)){if(recv_exact(cs,req+rn,1))goto out;rn++;req[rn]=0;if(strstr(req,"\r\n\r\n"))break;}if(!strstr(req,"Upgrade: websocket"))goto out;const char resp[]="HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Protocol: mqtt\r\nSec-WebSocket-Accept: AnW6Bcu1weOqCgyK7MOZnLJ+hSk=\r\n\r\n";if(send_all(cs,resp,sizeof(resp)-1))goto out;}
    if(broker_recv_packet(cs,c->ws,pkt,sizeof(pkt),&pn)||pn<2||pkt[0]!=0x10u)goto out;atomic_store(&c->stage,1);
    {const uint8_t ack[]={0x20,0x02,0x00,0x00};if(broker_send_packet(cs,c->ws,ack,sizeof(ack)))goto out;}
    if(broker_recv_packet(cs,c->ws,pkt,sizeof(pkt),&pn)||pn<2||(pkt[0]&0xF0u)!=0x80u)goto out;atomic_store(&c->stage,2);
    {const uint8_t suback[]={0x90,0x03,0x00,0x02,0x00};if(broker_send_packet(cs,c->ws,suback,sizeof(suback)))goto out;}
    {const char *t="from/broker",*d="hello-in";size_t tl=strlen(t),dl=strlen(d),rem=2+tl+dl;uint8_t pub[256];size_t n=0;pub[n++]=0x30;pub[n++]=(uint8_t)rem;pub[n++]=(uint8_t)(tl>>8);pub[n++]=(uint8_t)tl;memcpy(pub+n,t,tl);n+=tl;memcpy(pub+n,d,dl);n+=dl;if(broker_send_packet(cs,c->ws,pub,n))goto out;atomic_store(&c->stage,3);}
    if(broker_recv_packet(cs,c->ws,pkt,sizeof(pkt),&pn)||!mqtt_find_topic_payload(pkt,pn,"to/broker","hello-out"))goto out;
    atomic_store(&c->ok,1);
out:if(cs>=0)close(cs);if(ls>=0)close(ls);if(!atomic_load(&c->ready))atomic_store(&c->ready,-1);return NULL;
}
static int run_one(int ws){
    struct broker_ctx c={.ws=ws};pthread_t th;uint8 kind=0,data[64];char topic[128];uint32 dlen=0;
    atomic_init(&c.port,0);atomic_init(&c.ready,0);atomic_init(&c.ok,0);atomic_init(&c.stage,0);if(pthread_create(&th,NULL,broker_thread,&c))return 1;for(int i=0;i<3000&&!atomic_load(&c.ready);i++)usleep(1000);if(atomic_load(&c.ready)!=1)return 2;
    memset(&test_state,0,sizeof(test_state));test_state.emulation_model=1;test_state.at_firmware_profile=CU_ESP_AT_FW_ESPAT_230;test_state.mqtt_keepalive=30;snprintf((char*)test_state.mqtt_client_id,sizeof(test_state.mqtt_client_id),"cuzebox-test");
    if(cu_esp_mqtt_connect("127.0.0.1",(uint16)atomic_load(&c.port),(uint8)(ws?6:1),ws?"/mqtt":"",0))return 3;
    if(cu_esp_mqtt_take_event(&kind,topic,sizeof(topic),data,sizeof(data),&dlen)!=1||kind!=CU_ESP_MQTT_EVT_CONNECTED)return 4;
    if(cu_esp_mqtt_subscribe("from/broker",0))return 5;
    for(int i=0;i<5000;i++){cu_esp_mqtt_tick();kind=0;dlen=0;if(cu_esp_mqtt_take_event(&kind,topic,sizeof(topic),data,sizeof(data),&dlen)==1&&kind==CU_ESP_MQTT_EVT_MESSAGE){if(strcmp(topic,"from/broker")||dlen!=8||memcmp(data,"hello-in",8))return 6;break;}if(i==4999){fprintf(stderr,"no event ws=%d state=%u stage=%d\n",ws,(unsigned)cu_esp_mqtt_state(),atomic_load(&c.stage));return 7;}usleep(1000);}
    if(cu_esp_mqtt_publish("to/broker",(const uint8*)"hello-out",9,0,0))return 8;
    for(int i=0;i<3000&&!atomic_load(&c.ok);i++){cu_esp_mqtt_tick();usleep(1000);}if(!atomic_load(&c.ok))return 9;
    cu_esp_mqtt_disconnect();pthread_join(th,NULL);return 0;
}

struct reconnect_ctx { _Atomic unsigned port; _Atomic int ready; _Atomic int ok; };
static void *reconnect_broker_thread(void *vp){
    struct reconnect_ctx*c=(struct reconnect_ctx*)vp;int ls=-1,cs=-1,one=1;struct sockaddr_in sa;socklen_t sl=sizeof(sa);uint8_t pkt[4096];size_t pn=0;
    ls=socket(AF_INET,SOCK_STREAM,0);if(ls<0)goto out;setsockopt(ls,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));memset(&sa,0,sizeof(sa));sa.sin_family=AF_INET;sa.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(bind(ls,(struct sockaddr*)&sa,sizeof(sa))||getsockname(ls,(struct sockaddr*)&sa,&sl)||listen(ls,2))goto out;atomic_store(&c->port,ntohs(sa.sin_port));atomic_store(&c->ready,1);
    for(int pass=0;pass<2;pass++){
        cs=accept(ls,NULL,NULL);if(cs<0)goto out;{struct timeval tv={5,0};setsockopt(cs,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));setsockopt(cs,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));}
        if(mqtt_packet_from_stream(cs,pkt,sizeof(pkt),&pn)||pn<2||pkt[0]!=0x10u)goto out;
        {const uint8_t ack[]={0x20,0x02,0x00,0x00};if(send_all(cs,ack,sizeof(ack)))goto out;}
        if(mqtt_packet_from_stream(cs,pkt,sizeof(pkt),&pn)||pn<2||(pkt[0]&0xF0u)!=0x80u)goto out;
        {const uint8_t suback[]={0x90,0x03,0x00,0x02,0x00};if(send_all(cs,suback,sizeof(suback)))goto out;}
        if(pass==0){shutdown(cs,SHUT_RDWR);close(cs);cs=-1;usleep(100000);}else{atomic_store(&c->ok,1);}
    }
out:if(cs>=0)close(cs);if(ls>=0)close(ls);if(!atomic_load(&c->ready))atomic_store(&c->ready,-1);return NULL;
}
static int run_reconnect(void){
    struct reconnect_ctx c;pthread_t th;uint8_t kind=0,data[8];char topic[16];uint32 dlen=0;int saw_disc=0,saw_conn=0;
    memset(&c,0,sizeof(c));if(pthread_create(&th,NULL,reconnect_broker_thread,&c))return 1;for(int i=0;i<3000&&!atomic_load(&c.ready);i++)usleep(1000);if(atomic_load(&c.ready)!=1)return 2;
    memset(&test_state,0,sizeof(test_state));test_state.emulation_model=1;test_state.at_firmware_profile=CU_ESP_AT_FW_ESPAT_230;snprintf((char*)test_state.mqtt_client_id,sizeof(test_state.mqtt_client_id),"reconnect-test");
    if(cu_esp_mqtt_connect("127.0.0.1",(uint16)atomic_load(&c.port),1,"",1))return 3;
    if(cu_esp_mqtt_take_event(&kind,topic,sizeof(topic),data,sizeof(data),&dlen)!=1||kind!=CU_ESP_MQTT_EVT_CONNECTED)return 4;
    if(cu_esp_mqtt_subscribe("re/sub",0))return 5;
    for(int i=0;i<5000;i++){
        cu_esp_mqtt_tick();kind=0;if(cu_esp_mqtt_take_event(&kind,topic,sizeof(topic),data,sizeof(data),&dlen)==1){if(kind==CU_ESP_MQTT_EVT_DISCONNECTED)saw_disc=1;else if(kind==CU_ESP_MQTT_EVT_CONNECTED&&saw_disc){saw_conn=1;break;}}
        usleep(1000);
    }
    for(int i=0;i<1000&&!atomic_load(&c.ok);i++){cu_esp_mqtt_tick();usleep(1000);}
    cu_esp_mqtt_disconnect();pthread_join(th,NULL);
    return (saw_disc&&saw_conn&&atomic_load(&c.ok))?0:6;
}

int main(void){int a=run_one(0);if(a){fprintf(stderr,"MQTT TCP failed %d\n",a);return 10+a;}int b=run_one(1);if(b){fprintf(stderr,"MQTT WS failed %d\n",b);return 30+b;}int c=run_reconnect();if(c){fprintf(stderr,"MQTT reconnect failed %d\n",c);return 50+c;}printf("esp_mqtt_regression: PASS (TCP + WebSocket + reconnect/resubscribe)\n");return 0;}
