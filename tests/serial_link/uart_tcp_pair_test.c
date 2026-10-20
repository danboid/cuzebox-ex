#include "cu_esp.h"
#include "cu_avr.h"
#include "cu_uart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static cu_state_esp_t test_state;
cu_state_esp_t* cu_esp_get_state(void){ return &test_state; }

/* ESP-module-only hooks are not used by the raw TCP endpoint test. */
void cu_esp_txi(sint32 v){ (void)v; }
void cu_esp_txl(const char *s, auint n){ (void)s; (void)n; }
void cu_esp_txp(const char *s){ (void)s; }
void cu_esp_txp_error(void){}
void cu_esp_txp_ok(void){}
void cu_esp_process_at(sint8 *b){ (void)b; }
sint32 cu_esp_process_ipd(void){ return 0; }
void cu_esp_service_host_state(void){}
auint cu_esp_update_timer_counts(auint cycle){ return cycle; }
auint cu_esp_get_time_seconds(void){ return 1u; }
const char default_sntp_server0[] = "";
const char default_sntp_server1[] = "";
const char default_sntp_server2[] = "";
void cu_esp_lan_init(cu_state_esp_t *s){ (void)s; }
void cu_esp_lan_shutdown(cu_state_esp_t *s){ (void)s; }
void cu_esp_lan_tick(cu_state_esp_t *s, uint32 n){ (void)s; (void)n; }
void cu_esp_lan_on_softap_change(cu_state_esp_t *s){ (void)s; }
uint8 cu_esp_lan_virtual_link_present(cu_state_esp_t *s, uint32 x){ (void)s; (void)x; return 0u; }
uint8 cu_esp_lan_virtual_link_open(cu_state_esp_t *s, uint32 x){ (void)s; (void)x; return 0u; }
sint32 cu_esp_lan_virtual_open(cu_state_esp_t *s, uint32 x, const char *h, uint32 p, uint32 t, uint32 lp, uint32 u){ (void)s;(void)x;(void)h;(void)p;(void)t;(void)lp;(void)u; return -1; }
sint32 cu_esp_lan_virtual_send(cu_state_esp_t *s, uint32 x, const uint8 *b, uint16 l){ (void)s;(void)x;(void)b;(void)l; return -1; }
sint32 cu_esp_lan_virtual_recv(cu_state_esp_t *s, uint32 x, uint8 *b, uint16 l){ (void)s;(void)x;(void)b;(void)l; return -1; }
void cu_esp_lan_virtual_close(cu_state_esp_t *s, uint32 x, uint8 n){ (void)s;(void)x;(void)n; }
sint32 cu_esp_serial_midi_start(auint route){ (void)route; return -1; }
void cu_esp_serial_midi_end(void){}
void cu_esp_serial_midi_write(uint8 c){ (void)c; }
uint8 cu_esp_serial_midi_read(void){ return 0u; }
auint cu_esp_serial_midi_rx_bytes_ready(void){ return 0u; }

static uint8 checksum(const uint8 *p){
    uint8 v = 0x5Au;
    unsigned i;
    for(i = 0; i < 6; ++i) v = (uint8)((v << 1) | (v >> 7)) ^ p[i];
    return v;
}

static void make_packet(uint8 *p, unsigned role, unsigned seq, unsigned tick){
    p[0] = 0xC3u; p[1] = (uint8)role;
    p[2] = (uint8)seq; p[3] = (uint8)(seq >> 8);
    p[4] = (uint8)tick; p[5] = (uint8)(tick >> 8);
    p[6] = checksum(p); p[7] = 0x3Cu;
}

int main(int argc, char **argv){
    const auint tick_cycles = 953680u; /* 30 Hz at 28.63636 MHz */
    const auint poll_cycles = 1820u;   /* roughly one scanline */
    unsigned role, port, tick, tx_seq = 0u, rx_packets = 0u;
    unsigned last_rx_tick = 0u, max_gap = 0u, bad = 0u, rx_have = 0u;
    unsigned tx_pos = 8u, tx_active = 0u;
    uint8 tx[8], rx[8];
    auint cycle = 0u;

    unsigned net_mode = 0u; /* 0 = TCP, 3 = internet relay, 4 = LAN discovery */
    if(argc != 4 && argc != 5){ fprintf(stderr, "usage: %s server|client port ticks | internet relay_port ticks role | lan tcp_port ticks role\n", argv[0]); return 2; }
    if(strcmp(argv[1], "internet") == 0) net_mode = CU_ESP_TCP_SERIAL_MODE_INTERNET;
    else if(strcmp(argv[1], "lan") == 0) net_mode = CU_ESP_TCP_SERIAL_MODE_LAN;
    if(net_mode != 0u && argc != 5){ fprintf(stderr, "role (0|1) required\n"); return 2; }
    role = net_mode ? (unsigned)strtoul(argv[4], NULL, 10) : (strcmp(argv[1], "server") == 0 ? 0u : 1u);
    port = (unsigned)strtoul(argv[2], NULL, 10);

    memset(&test_state, 0, sizeof(test_state));
    test_state.serial_route = CU_ESP_SERIAL_TCP_SERIAL;
    test_state.uart_profile = CU_UART_PROFILE_FAST;
    test_state.uart_baud_bits = 185u;
    test_state.uart_data_bits = 8u;
    test_state.uart_stop_bits = 1u;
    test_state.uart_rx_enabled = 1u;
    test_state.uart_tx_enabled = 1u;
    test_state.tcp_serial_auto_reconnect = 1u;
    test_state.tcp_serial_retry_interval_ms = 10u;
    test_state.tcp_serial_port = port;
    test_state.tcp_serial_mode = role == 0u ? CU_ESP_TCP_SERIAL_MODE_SERVER : CU_ESP_TCP_SERIAL_MODE_CLIENT;
    snprintf((char*)test_state.tcp_serial_host, sizeof(test_state.tcp_serial_host), "127.0.0.1");
    if(net_mode == CU_ESP_TCP_SERIAL_MODE_INTERNET){
        test_state.tcp_serial_mode = net_mode;
        test_state.tcp_serial_port = 12001u;
        cu_esp_link_set_room("PAIRTEST");
        cu_esp_link_set_relay_host("127.0.0.1");
        cu_esp_link_set_relay_port(port);
    }else if(net_mode == CU_ESP_TCP_SERIAL_MODE_LAN){
        test_state.tcp_serial_mode = net_mode;
        cu_esp_link_set_rom_id(0x1234ABCDu);
    }

    if(cu_esp_tcp_serial_start() != 0) return 3;
    for(tick = 0; tick < 10000u && cu_esp_get_tcp_serial_state() != CU_ESP_TCP_SERIAL_STATE_CONNECTED; ++tick){
        cu_esp_endpoint_host_tick();
        (void)cu_esp_tcp_serial_rx_bytes_ready();
        usleep(1000);
    }
    if(cu_esp_get_tcp_serial_state() != CU_ESP_TCP_SERIAL_STATE_CONNECTED){ fprintf(stderr, "%s connect timeout\n", argv[1]); return 4; }

    for(tick = 0; tick < (unsigned)strtoul(argv[3], NULL, 10); ++tick){
        cu_esp_endpoint_host_tick();
        auint end = WRAP32(cycle + tick_cycles);
        if(!tx_active){ make_packet(tx, role, tx_seq++, tick); tx_pos = 0u; tx_active = 1u; }

        for(;;){
            auint st = cu_esp_uzebox_status(cycle);
            if(tx_active && tx_pos < 8u && (st & (1u << UDRE0))){
                cu_esp_uzebox_write(tx[tx_pos++], cycle);
                if(tx_pos == 8u) tx_active = 0u;
            }
            if(st & (1u << RXC0)){
                uint8 b = (uint8)cu_esp_uzebox_read(cycle);
                if(rx_have == 0u && b != 0xC3u){ bad++; }
                else{
                    rx[rx_have++] = b;
                    if(rx_have == 8u){
                        if(rx[0] != 0xC3u || rx[7] != 0x3Cu || rx[1] == role || rx[6] != checksum(rx)) bad++;
                        else{
                            unsigned gap = rx_packets ? tick - last_rx_tick : 0u;
                            if(gap > max_gap) max_gap = gap;
                            last_rx_tick = tick;
                            rx_packets++;
                        }
                        rx_have = 0u;
                    }
                }
            }
            if(WRAP32(end - cycle) <= poll_cycles){ cycle = end; break; }
            cycle = WRAP32(cycle + poll_cycles);
        }
        usleep(1000);
    }

    /* Finish any frame in flight and receive the peer's tail. */
    unsigned done_tick = 0xFFFFFFFFu;
    for(tick = 0; tick < (net_mode ? 20000u : 400u); ++tick){
        auint end = WRAP32(cycle + tick_cycles), pos = cycle;
        cu_esp_endpoint_host_tick();
        while(WRAP32(end - pos) > poll_cycles){
            auint st;
            pos = WRAP32(pos + poll_cycles);
            st = cu_esp_uzebox_status(pos);
            if(tx_active && tx_pos < 8u && (st & (1u << UDRE0))){
                cu_esp_uzebox_write(tx[tx_pos++], pos);
                if(tx_pos == 8u) tx_active = 0u;
            }
            if(st & (1u << RXC0)){
                uint8 b = (uint8)cu_esp_uzebox_read(pos);
                if(rx_have == 0u && b != 0xC3u) bad++;
                else if(rx_have < 8u){ rx[rx_have++] = b; if(rx_have == 8u){ if(rx[6] != checksum(rx)) bad++; else rx_packets++; rx_have=0u; } }
            }
        }
        cycle = end; usleep(1000);
        if(tick >= 32u && rx_packets >= tx_seq && !tx_active){
            /* Network modes: linger so the peer's retransmissions/acks finish
             * before this side closes the link. */
            if(net_mode == 0u) break;
            if(done_tick == 0xFFFFFFFFu) done_tick = tick;
            if(tick >= done_tick + 1500u) break;
        }
    }

    printf("%s tx=%u rx=%u bad=%u max_gap_ticks=%u final_cycle=%08X\n",
           argv[1], tx_seq, rx_packets, bad, max_gap, (unsigned)cycle);
    cu_esp_tcp_serial_end();
    if(bad != 0u || rx_packets != tx_seq || (net_mode == 0u && max_gap > 20u)) return 5;
    return 0;
}

uint8 cu_esp_lan_local_destination(cu_state_esp_t *s,const char *h){(void)s;(void)h;return 0;}
