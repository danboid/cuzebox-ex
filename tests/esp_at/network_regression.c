#include "cu_esp.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
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

struct echo_ctx { int family; _Atomic unsigned port; _Atomic int ready; _Atomic int ok; };
static void *echo_server(void *vp){
    struct echo_ctx *c=vp; int ls=-1,cs=-1,one=1; unsigned char b=0; socklen_t sl;
    ls=socket(c->family,SOCK_STREAM,0); if(ls<0){atomic_store(&c->ready,-1);return NULL;}
    setsockopt(ls,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
    if(c->family==AF_INET){ struct sockaddr_in sa; memset(&sa,0,sizeof(sa));sa.sin_family=AF_INET;sa.sin_addr.s_addr=htonl(INADDR_LOOPBACK);sl=sizeof(sa);if(bind(ls,(struct sockaddr*)&sa,sizeof(sa))||getsockname(ls,(struct sockaddr*)&sa,&sl))goto out;atomic_store(&c->port,ntohs(sa.sin_port)); }
    else { struct sockaddr_in6 sa; memset(&sa,0,sizeof(sa));sa.sin6_family=AF_INET6;sa.sin6_addr=in6addr_loopback;sl=sizeof(sa);if(bind(ls,(struct sockaddr*)&sa,sizeof(sa))||getsockname(ls,(struct sockaddr*)&sa,&sl))goto out;atomic_store(&c->port,ntohs(sa.sin6_port)); }
    if(listen(ls,1))goto out; atomic_store(&c->ready,1); cs=accept(ls,NULL,NULL); if(cs<0)goto out;
    if(recv(cs,&b,1,0)!=1||b!=0xA5)goto out; b=0x5A;if(send(cs,&b,1,0)!=1)goto out;atomic_store(&c->ok,1);
out: if(cs>=0)close(cs);if(ls>=0)close(ls);if(!atomic_load(&c->ready))atomic_store(&c->ready,-1);return NULL;
}
static int wait_open(unsigned link){ for(unsigned i=0;i<7000;i++){cu_esp_net_tick();if(cu_esp_net_link_state(link)==CU_ESP_LS_OPEN)return 1;if(cu_esp_net_link_state(link)==CU_ESP_LS_ERROR)return 0;usleep(1000);}return 0; }
static int xfer(unsigned link){ unsigned char a=0xA5,b=0;
    if(cu_esp_net_rx_ready_mask() & (1u<<link)) return 0;
    for(unsigned i=0;i<3000;i++){sint32 r=cu_esp_net_send(link,(sint8*)&a,1,0);if(r==1)break;if(i==2999)return 0;cu_esp_net_tick();usleep(1000);}
    for(unsigned i=0;i<3000;i++){if(cu_esp_net_rx_ready_mask()&(1u<<link)){sint32 r=cu_esp_net_recv(link,(sint8*)&b,1,0);if(r==1)return b==0x5A;}cu_esp_net_tick();usleep(1000);}return 0; }
int main(void){
    struct echo_ctx c4={.family=AF_INET},c6={.family=AF_INET6};pthread_t t4,t6;char peer[96];uint16 pp=0;int have6=1;
    atomic_init(&c4.port,0);atomic_init(&c4.ready,0);atomic_init(&c4.ok,0);
    memset(&test_state,0,sizeof(test_state));test_state.emulation_model=1;test_state.at_firmware_profile=CU_ESP_AT_FW_ESPAT_230;
    cu_esp_reset_network();
    if(pthread_create(&t4,NULL,echo_server,&c4))return 10;for(unsigned i=0;i<2000&&!atomic_load(&c4.ready);i++)usleep(1000);if(atomic_load(&c4.ready)!=1)return 11;
    if(cu_esp_net_connect_ex((sint8*)"127.0.0.1",4,atomic_load(&c4.port),ESP_PROTO_TCP,0,0))return 12;
    if(!wait_open(4))return 13;if(cu_esp_net_get_peer_text(4,peer,sizeof(peer),&pp)||strcmp(peer,"127.0.0.1"))return 14;if(!xfer(4))return 15;pthread_join(t4,NULL);if(!atomic_load(&c4.ok))return 16;cu_esp_close_socket(4);

    atomic_init(&c6.port,0);atomic_init(&c6.ready,0);atomic_init(&c6.ok,0);
    if(pthread_create(&t6,NULL,echo_server,&c6)) have6=0; else {for(unsigned i=0;i<2000&&!atomic_load(&c6.ready);i++)usleep(1000);if(atomic_load(&c6.ready)!=1)have6=0;}
    if(have6){ test_state.ipv6_enabled=1;if(cu_esp_net_connect_ex((sint8*)"::1",3,atomic_load(&c6.port),ESP_PROTO_TCP|ESP_PROTO_IPV6,0,0))return 20;if(!wait_open(3))return 21;if(cu_esp_net_get_peer_text(3,peer,sizeof(peer),&pp)||strchr(peer,':')==NULL)return 22;if(!xfer(3))return 23;pthread_join(t6,NULL);if(!atomic_load(&c6.ok))return 24;cu_esp_close_socket(3);
        if(cu_esp_dns_async_start_ex("localhost",3)!=0)return 25;for(unsigned i=0;i<5000;i++){char out[64];sint32 r;cu_esp_net_tick();r=cu_esp_dns_async_poll_text(out,sizeof(out));if(r>0){if(strchr(out,':')==NULL)return 26;goto dns_ok;}if(r<0)return 27;usleep(1000);}return 28;dns_ok:; }
    else if(atomic_load(&c6.ready)==1) pthread_join(t6,NULL); else pthread_join(t6,NULL);
    cu_esp_net_cleanup();
    printf("esp_network_regression: PASS (link4 IPv4, IPv6=%s)\n",have6?"PASS":"SKIP");return 0;
}
