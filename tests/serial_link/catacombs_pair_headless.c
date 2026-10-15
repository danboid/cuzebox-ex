#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "cu_avr.h"
#include "cu_ctr.h"
#include "cu_esp.h"

#ifndef GAME_LOCAL_PLAYER_ADDR
#define GAME_LOCAL_PLAYER_ADDR 0x0A4Au
#endif
#ifndef GAME_RUN_MODE_ADDR
#define GAME_RUN_MODE_ADDR 0x0A4Bu
#endif
#ifndef GAME_STATE_ADDR
#define GAME_STATE_ADDR 0x0A4Fu
#endif
#ifndef GAME_LOCAL_TOKEN_ADDR
#define GAME_LOCAL_TOKEN_ADDR 0x0A56u
#endif
#ifndef GAME_REMOTE_TOKEN_ADDR
#define GAME_REMOTE_TOKEN_ADDR 0x0A57u
#endif
#ifndef GAME_CONNECT_PHASE_ADDR
#define GAME_CONNECT_PHASE_ADDR 0x0A58u
#endif
#ifndef GAME_TICK_FRAME_ADDR
#define GAME_TICK_FRAME_ADDR 0x0A1Bu
#endif
#ifndef GAME_LINK_LOST_FRAMES_ADDR
#define GAME_LINK_LOST_FRAMES_ADDR 0x0A2Fu
#endif


static cu_state_esp_t headless_esp;
cu_state_esp_t* cu_esp_get_state(void){ return &headless_esp; }
void cu_esp_reset_pin(uint8 state, auint cycle){ (void)state; (void)cycle; }
auint cu_gun_build_packet(sint32 wx, sint32 wy, auint trigger_down, auint* light_out){ (void)wx;(void)wy;(void)trigger_down; if(light_out)*light_out=0; return 0; }
/* Minimal nonblocking TCP byte cable for the headless two-console regression. */
static int cable_fd=-1; static int cable_closed=0;
static uint8 cable_rx[4096], cable_tx[4096];
static unsigned cable_rx_h=0,cable_rx_t=0,cable_rx_n=0,cable_tx_h=0,cable_tx_t=0,cable_tx_n=0;
static void cable_pump(void){
  if(cable_closed) return;
  while(cable_tx_n){
    unsigned chunk=sizeof(cable_tx)-cable_tx_h; if(chunk>cable_tx_n)chunk=cable_tx_n;
    ssize_t n=send(cable_fd,cable_tx+cable_tx_h,chunk,MSG_NOSIGNAL);
    if(n>0){ cable_tx_h=(cable_tx_h+(unsigned)n)%sizeof(cable_tx); cable_tx_n-=(unsigned)n; continue; }
    if(errno==EAGAIN||errno==EWOULDBLOCK) break; perror("send"); exit(3);
  }
  while(cable_rx_n<sizeof(cable_rx)){
    unsigned free_n=sizeof(cable_rx)-cable_rx_n;
    unsigned chunk=sizeof(cable_rx)-cable_rx_t; if(chunk>free_n)chunk=free_n;
    ssize_t n=recv(cable_fd,cable_rx+cable_rx_t,chunk,0);
    if(n>0){ cable_rx_t=(cable_rx_t+(unsigned)n)%sizeof(cable_rx); cable_rx_n+=(unsigned)n; continue; }
    if(n==0){ cable_closed=1; return; }
    if(errno==EAGAIN||errno==EWOULDBLOCK) break; perror("recv"); exit(3);
  }
}
sint32 cu_esp_host_serial_start(void){ return -1; }
void cu_esp_host_serial_end(void){}
void cu_esp_host_serial_write(uint8 c){ (void)c; }
uint8 cu_esp_host_serial_read(void){ return 0; }
auint cu_esp_host_serial_rx_bytes_ready(void){ return 0; }
sint32 cu_esp_tcp_serial_start(void){ return 0; }
void cu_esp_tcp_serial_end(void){}
void cu_esp_tcp_serial_write(uint8 c){
  if(cable_tx_n>=sizeof(cable_tx)){ fprintf(stderr,"tx overflow\n"); exit(3); }
  cable_tx[cable_tx_t]=c; cable_tx_t=(cable_tx_t+1)%sizeof(cable_tx); cable_tx_n++; cable_pump();
}
uint8 cu_esp_tcp_serial_read(void){ uint8 c=0; cable_pump(); if(cable_rx_n){ c=cable_rx[cable_rx_h]; cable_rx_h=(cable_rx_h+1)%sizeof(cable_rx); cable_rx_n--; } return c; }
auint cu_esp_tcp_serial_rx_bytes_ready(void){ cable_pump(); return cable_rx_n; }
void cu_esp_serial_midi_write(uint8 c){ (void)c; }
uint8 cu_esp_serial_midi_read(void){ return 0; }
auint cu_esp_serial_midi_rx_bytes_ready(void){ return 0; }
void cu_esp_txp(const char *s){ (void)s; }
void cu_esp_txl(const char *s, auint l){ (void)s;(void)l; }
void cu_esp_txi(sint32 v){ (void)v; }
void cu_esp_txp_error(void){}
void cu_esp_txp_ok(void){}
sint32 cu_esp_mqtt_publish(const char *topic, const uint8 *data, uint32 len, uint8 qos, uint8 retain){(void)topic;(void)data;(void)len;(void)qos;(void)retain;return -1;}
sint32 cu_esp_net_send(uint32 s, sint8 *b, sint32 l, sint32 f){ (void)s;(void)b;(void)l;(void)f; return -1; }
void cu_esp_process_at(sint8 *b){ (void)b; }
void cu_esp_save_uart(void){}
void cu_esp_clear_at_command(void){}
auint cu_esp_update_timer_counts(auint cycle){ return cycle; }
sint32 cu_esp_process_ipd(void){ return 0; }
void cu_esp_service_host_state(void){}
void cu_esp_runtime_shutdown(void){}
boole cu_esp_config_live_ready(void){ return TRUE; }
auint cu_esp_load_config(void){ return 1; }
void cu_esp_apply_serial_route_live(void){}
auint cu_kbd_enabled = 0;
auint cu_kbd_state=0, cu_kbd_clock=0, cu_kbd_data_in=0, cu_kbd_data_out=0, cu_kbd_bypassed=0;
auint cu_kbd_queue_in=0, cu_kbd_queue_out=0, cu_kbd_held_lctrl=0;
uint8 cu_kbd_queue[512];
uint8 cu_mouse_enabled=0, cu_mouse_buttons=0, cu_mouse_scale=0;
sint32 cu_mouse_dx=0, cu_mouse_dy=0;

auint cu_hap_process(auint prev, auint curr){ (void)prev; return curr; }
auint cu_kbd_process(auint prev, auint curr){ (void)prev; return curr; }

/* SPI is irrelevant for this boot/link test. */
void cu_spi_reset(auint cycle){ (void)cycle; }
void cu_spi_cs_set(auint id, boole ena, auint cycle){ (void)id; (void)ena; (void)cycle; }
void cu_spi_send(auint data, auint cycle){ (void)data; (void)cycle; }
auint cu_spi_recv(auint cycle){ (void)cycle; return 0xFFu; }

/* Optional input devices are disabled. */
void cu_gun_reset(void){}
void cu_gun_latch_event(void){}
auint cu_gun_any_enabled(void){ return 0; }
auint cu_gun_get_enabled(auint p){ (void)p; return 0; }
auint cu_gun_get_latched(auint p){ (void)p; return 0; }
void cu_hap_reset_sessions(void){}
void cu_hap_route_payload_port(auint p, uint8 v){ (void)p; (void)v; }
auint cu_kbd_get_enabled(void){ return 0; }
void cu_kbd_reset_sessions(void){}
auint cu_kbd_response_for_port_command(auint p, auint c){ (void)p; (void)c; return 0xFF; }
void cu_kbd_set_vdev_session(auint p, boole e){ (void)p; (void)e; }
auint cu_mouse_get_enabled(void){ return 0; }

auint ginput_host_get_controller_buttons(auint i){ (void)i; return 0; }
auint ginput_host_get_keyboard_buttons(void){ return 0; }
auint ginput_host_get_mouse_buttons(void){ return 0; }
void ginput_host_consume_mouse_delta(sint32 *x, sint32 *y){ if(x)*x=0; if(y)*y=0; }

static int hexbyte(const char *p){ char b[3]={p[0],p[1],0}; return (int)strtol(b,NULL,16); }
static int load_hex(const char *path){
  FILE *f=fopen(path,"r"); char line[1024]; uint32_t base=0; cu_state_cpu_t *s=cu_avr_get_state();
  if(!f){perror(path); return 0;} memset(s->crom,0xFF,sizeof(s->crom));
  while(fgets(line,sizeof(line),f)){
    if(line[0]!=':') continue; int n=hexbyte(line+1); uint32_t a=(hexbyte(line+3)<<8)|hexbyte(line+5); int t=hexbyte(line+7);
    if(t==0){ for(int i=0;i<n;i++){ uint32_t x=base+a+i; if(x<sizeof(s->crom)) s->crom[x]=(uint8)hexbyte(line+9+i*2); } }
    else if(t==4){ base=((uint32_t)((hexbyte(line+9)<<8)|hexbyte(line+11)))<<16; }
    else if(t==1) break;
  }
  fclose(f); return 1;
}

static void run_frame(unsigned *rows_out, uint32_t *cycles_out){
  unsigned rows=0; uint32_t start=cu_avr_getcycle();
  while(rows<400){ auint r=cu_avr_run(); if(r & CU_GET_ROW) rows++; if(r & CU_GET_FRAME) break; }
  *rows_out=rows; *cycles_out=(uint32_t)(cu_avr_getcycle()-start);
}

static int open_cable(const char *role, unsigned port){
  int fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0){perror("socket");return -1;}
  struct sockaddr_in a; memset(&a,0,sizeof(a)); a.sin_family=AF_INET; a.sin_port=htons((uint16_t)port); a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  if(strcmp(role,"server")==0){
    int one=1; setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
    if(bind(fd,(struct sockaddr*)&a,sizeof(a))<0||listen(fd,1)<0){perror("listen");return -1;}
    int c=accept(fd,NULL,NULL); if(c<0){perror("accept");return -1;} close(fd); fd=c;
  }else{
    for(int i=0;i<200;i++){ if(connect(fd,(struct sockaddr*)&a,sizeof(a))==0) break; if(errno!=ECONNREFUSED){perror("connect");return -1;} usleep(10000); if(i==199){fprintf(stderr,"connect timeout\n");return -1;} }
  }
  int fl=fcntl(fd,F_GETFL,0); fcntl(fd,F_SETFL,fl|O_NONBLOCK); return fd;
}

int main(int argc,char **argv){
  if(argc<4){fprintf(stderr,"usage: %s rom.hex server|client port\n",argv[0]);return 2;}
  int is_server=!strcmp(argv[2],"server"); cable_fd=open_cable(argv[2],(unsigned)strtoul(argv[3],NULL,10)); if(cable_fd<0)return 2;
  if(!load_hex(argv[1])) return 2;
  cu_state_esp_t *e=cu_esp_get_state(); memset(e,0,sizeof(*e)); e->serial_route=CU_ESP_SERIAL_TCP_SERIAL; e->uart_profile=CU_UART_PROFILE_FAST; /* Match the normal CUzeBox default; raw endpoints still use hardware buffering. */
  cu_avr_autofuse(FALSE); cu_ctr_reset(); cu_avr_reset();
  cu_esp_serial_trace_set_enabled(TRUE);
  unsigned rows; uint32_t cyc; unsigned last_state=255,last_phase=255; int success=0;
  unsigned gameplay_start=0, last_tick_change_frame=0, max_tick_stall=0;
  unsigned target_reached_frame=0;
  int tick_progress_started=0;
  uint16_t last_tick=0;
  for(unsigned f=0;f<5000;f++){
    cu_ctr_setsnes(0,0);
    if(f==60){
      cu_state_cpu_t *c=cu_avr_get_state();
      c->sram[GAME_STATE_ADDR]=1; c->sram[GAME_LOCAL_PLAYER_ADDR]=0; c->sram[GAME_RUN_MODE_ADDR]=1;
      c->sram[GAME_LOCAL_TOKEN_ADDR]=is_server?0x31:0x53; c->sram[GAME_REMOTE_TOKEN_ADDR]=0; c->sram[GAME_CONNECT_PHASE_ADDR]=0;
      c->iors[CU_IO_UBRR0H]=0; cu_esp_uzebox_modify(CU_IO_UBRR0H,0,c->cycle);
      c->iors[CU_IO_UBRR0L]=185; cu_esp_uzebox_modify(CU_IO_UBRR0L,185,c->cycle);
      c->iors[CU_IO_UCSR0A]=0; cu_esp_uzebox_modify(CU_IO_UCSR0A,0,c->cycle);
      c->iors[CU_IO_UCSR0B]=(1u<<RXEN0)|(1u<<TXEN0); cu_esp_uzebox_modify(CU_IO_UCSR0B,c->iors[CU_IO_UCSR0B],c->cycle);
      c->iors[CU_IO_UCSR0C]=(1u<<UCSZ01)|(1u<<UCSZ00); cu_esp_uzebox_modify(CU_IO_UCSR0C,c->iors[CU_IO_UCSR0C],c->cycle);
    }
    run_frame(&rows,&cyc); cable_pump();
    cu_state_cpu_t *cpu=cu_avr_get_state();
    unsigned st=cpu->sram[GAME_STATE_ADDR], ph=cpu->sram[GAME_CONNECT_PHASE_ADDR];
    uint16_t tick=(uint16_t)cpu->sram[GAME_TICK_FRAME_ADDR] |
                  ((uint16_t)cpu->sram[GAME_TICK_FRAME_ADDR+1u]<<8);
    if(st!=last_state||ph!=last_phase||rows!=262||cyc!=476840u){
      printf("%s f=%u state=%u phase=%u tick=%u rows=%u cycles=%u rx=%u tx=%u\n",
             argv[2],f,st,ph,(unsigned)tick,rows,cyc,cable_rx_n,cable_tx_n);
      fflush(stdout); last_state=st;last_phase=ph;
    }
    if(f>60 && st!=1 && !success){ success=1; }
    if(st==3){
      if(gameplay_start==0u){
        gameplay_start=f;
        last_tick=tick;
        last_tick_change_frame=f;
      }else if(tick!=last_tick){
        if(tick_progress_started){
          unsigned stall=f-last_tick_change_frame;
          if(stall>max_tick_stall) max_tick_stall=stall;
          if(stall>8u)
            fprintf(stderr,"%s tick %u waited %u video frames (link_wait=%u)\n",
                    argv[2],(unsigned)tick,stall,
                    (unsigned)cpu->sram[GAME_LINK_LOST_FRAMES_ADDR]);
        }else{
          tick_progress_started=1;
        }
        last_tick=tick;
        last_tick_change_frame=f;
      }
      if(tick>=1000u){
        if(target_reached_frame==0u)
          target_reached_frame=f;
        /* Keep servicing the cable briefly so the peer can reach the same
         * target before either process closes its socket. */
        if(f-target_reached_frame>=120u){
          if(max_tick_stall>8u){
            fprintf(stderr,"%s active gameplay stuttered for %u video frames\n",
                    argv[2],max_tick_stall);
            close(cable_fd);
            return 1;
          }
          printf("%s gameplay reached target tick=1000 actual_tick=%u max_active_video_frame_stall=%u link_wait=%u\n",
                 argv[2],(unsigned)tick,max_tick_stall,
                 (unsigned)cpu->sram[GAME_LINK_LOST_FRAMES_ADDR]);
          close(cable_fd);
          return 0;
        }
      }
    }else if(gameplay_start!=0u && st==5){
      fprintf(stderr,
              "%s link failed at tick=%u after %u gameplay video frames; max stall=%u link_wait=%u\n",
              argv[2],(unsigned)last_tick,f-gameplay_start,max_tick_stall,
              (unsigned)cpu->sram[GAME_LINK_LOST_FRAMES_ADDR]);
      close(cable_fd);
      return 1;
    }
    /* Pace the two host processes enough for fair scheduling. Without this,
     * one process can emulate an OS timeslice worth of video frames while its
     * peer is descheduled, which is not representative of two 60 Hz consoles.
     */
    usleep(5000);
  }
  close(cable_fd);
  if(!success){fprintf(stderr,"%s did not leave serial-connect state\n",argv[2]);return 1;}
  {
    char trace_path[96];
    snprintf(trace_path,sizeof(trace_path),"catacombs-%s-uart-raw.txt",argv[2]);
    cu_esp_serial_trace_export(trace_path,TRUE);
  }
  fprintf(stderr,"%s gameplay did not reach tick 1000 (last tick=%u max stall=%u)\n",
          argv[2],(unsigned)last_tick,max_tick_stall);
  return 1;
}
