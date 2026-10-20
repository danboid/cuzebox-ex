/* Real multicast discovery, DHCP join and Tank Fu's bidirectional UDP ports. */
#include "cu_esp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>

static cu_state_esp_t state;
cu_state_esp_t *cu_esp_get_state(void){return &state;}
auint cu_esp_get_time_seconds(void){return (auint)time(NULL);}
void cu_message(char const *f,...){(void)f;}
void cu_error(char const *f,...){(void)f;}
void cu_unf(char const *s){(void)s;}
void cu_esp_txp(const char *s){(void)s;}
void cu_esp_txl(const char *s,auint n){(void)s;(void)n;}
void cu_esp_txi(sint32 n){(void)n;}
void cu_esp_txp_ok(void){}

#define CHECK(x,m) do{if(!(x)){fprintf(stderr,"FAIL %s: %s (line %d)\n",station?"station":"AP",m,__LINE__);return 1;}}while(0)
static void tick(void){cu_esp_net_tick();cu_esp_ap_join_tick(&state,28636U);usleep(1000);}

static int peer(int station)
{
 uint8 tx[768],rx[768];unsigned n;int got=0;
 srand((unsigned)getpid());cu_esp_reset_factory();
 state.emulation_model=1;cu_esp_reset_network();state.ip_delay_timer=0;
 state.wifi_mode=station?ESP_WIFI_MODE_STATION:ESP_WIFI_MODE_SOFTAP;
 snprintf((char*)state.soft_ap_name,sizeof(state.soft_ap_name),"TankFu-regression");
 snprintf((char*)state.soft_ap_pass,sizeof(state.soft_ap_pass),"T4nkFuN3t");
 state.soft_ap_encryption=3;state.soft_ap_max_conn=1;
 cu_esp_ap_set_softap_ip(&state,(sint8*)"192.168.4.1",(sint8*)"192.168.4.1",NULL);
 cu_esp_set_softap_enabled(TRUE);cu_esp_lan_init(&state);
 if(station){
  for(n=0;n<5000 && cu_esp_ap_find_discovered(&state,"TankFu-regression",NULL)<0;n++)tick();
  CHECK(n<5000,"discover advertised AP");
  CHECK(cu_esp_ap_start_join(&state,"TankFu-regression","wrong-password",NULL)==0,"begin rejected join");
  for(n=0;n<5000 && state.lan.join_req_pending;n++)tick();
  CHECK(n<5000 && !state.lan.joined,"wrong password must not associate");
  CHECK(cu_esp_ap_start_join(&state,"TankFu-regression","T4nkFuN3t",NULL)==0,"begin authenticated join");
  for(n=0;n<5000 && !state.lan.joined;n++)tick();
  CHECK(state.lan.joined,"DHCP join");
  CHECK(cu_esp_ap_set_station_ip(&state,(sint8*)"192.168.4.2",NULL,NULL)==0,"Tank Fu static IP after DHCP");
  for(n=0;n<5000 && state.lan.join_req_pending;n++)tick();
  CHECK(state.lan.joined && !state.lan.join_req_pending && !strcmp((char*)state.station_ip,"192.168.4.2"),"static lease refresh");
 }
 CHECK(cu_esp_net_connect_ex((sint8*)(station?"192.168.4.1":"192.168.4.2"),0,
       station?2233:1001,ESP_PROTO_UDP,station?1001:2233,0)==0,"open UDP");
 for(n=0;n<2000 && cu_esp_net_link_state(0)!=CU_ESP_LS_OPEN;n++)tick();
 CHECK(cu_esp_lan_virtual_link_open(&state,0),"virtual LAN destination must not use host routing");
 CHECK(cu_esp_net_get_local_port(0)==(station?1001:2233),"report virtual UDP local port");
 CHECK(cu_esp_net_connect_ex((sint8*)(station?"192.168.4.1":"192.168.4.2"),1,
       station?2233:1001,ESP_PROTO_UDP,station?1001:2233,0)<0,"duplicate UDP bind must fail");
 for(n=0;n<sizeof(tx);n++)tx[n]=(uint8)(n*17U+3U);
 for(n=0;n<7000;n++){
  tick();
  if(station && n%100==0)CHECK(cu_esp_net_send(0,(sint8*)tx,sizeof(tx),0)==sizeof(tx),"send station payload");
  if(cu_esp_net_rx_ready_mask()&1U){
   sint32 count=cu_esp_net_recv(0,(sint8*)rx,sizeof(rx),0);
   CHECK(count==sizeof(rx) && !memcmp(tx,rx,sizeof(rx)),"binary payload survives AP overlay");
   got=1;
   if(!station)CHECK(cu_esp_net_send(0,(sint8*)rx,sizeof(rx),0)==sizeof(rx),"reply from AP");
   else break;
  }
  if(!station && got && n>1500)break;
 }
 CHECK(got,"bidirectional Tank Fu UDP exchange");
 cu_esp_close_socket(0);cu_esp_set_softap_enabled(FALSE);cu_esp_net_cleanup();
 return 0;
}
int main(void)
{
 pid_t child=fork();int status=0,rc;
 if(child<0)return 2;
 if(!child)_exit(peer(1));
 rc=peer(0);waitpid(child,&status,0);
 if(rc || !WIFEXITED(status) || WEXITSTATUS(status))return 1;
 puts("SoftAP: discovery, DHCP, Tank Fu UDP ports and 768-byte binary round trip PASS");
 return 0;
}
