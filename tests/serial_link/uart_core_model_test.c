#include "cu_esp.h"
#include "cu_avr.h"
#include "cu_uart.h"
#include <stdio.h>
#include <string.h>

static cu_state_esp_t test_state;
static uint8 tcp_rx[32], tcp_tx[32];
static unsigned tcp_rx_pos, tcp_rx_len, tcp_tx_len;
static unsigned at_calls;
static char at_command[64];

cu_state_esp_t* cu_esp_get_state(void){ return &test_state; }

/* Host/MIDI endpoints are inert in this test. TCP uses deterministic byte
 * queues so its AVR-visible timing can be compared with loopback and ESP. */
void cu_esp_host_serial_write(uint8 c){ (void)c; }
uint8 cu_esp_host_serial_read(void){ return 0u; }
auint cu_esp_host_serial_rx_bytes_ready(void){ return 0u; }
void cu_esp_tcp_serial_write(uint8 c){ if(tcp_tx_len < sizeof(tcp_tx)) tcp_tx[tcp_tx_len++] = c; }
uint8 cu_esp_tcp_serial_read(void){ return (tcp_rx_pos < tcp_rx_len) ? tcp_rx[tcp_rx_pos++] : 0u; }
auint cu_esp_tcp_serial_rx_bytes_ready(void){ return (tcp_rx_pos < tcp_rx_len) ? 1u : 0u; }
void cu_esp_serial_midi_write(uint8 c){ (void)c; }
uint8 cu_esp_serial_midi_read(void){ return 0u; }
auint cu_esp_serial_midi_rx_bytes_ready(void){ return 0u; }
void cu_esp_txi(sint32 v){ (void)v; }
void cu_esp_txl(const char *s, auint n){ (void)s; (void)n; }
void cu_esp_txp(const char *s){ (void)s; }
void cu_esp_txp_error(void){}
void cu_esp_txp_ok(void){}
sint32 cu_esp_mqtt_publish(const char *topic, const uint8 *data, uint32 len, uint8 qos, uint8 retain){(void)topic;(void)data;(void)len;(void)qos;(void)retain;return -1;}
sint32 cu_esp_net_send(uint32 s, sint8 *b, sint32 l, sint32 f){ (void)s; (void)b; (void)l; (void)f; return -1; }
void cu_esp_process_at(sint8 *b){
    at_calls++;
    snprintf(at_command, sizeof(at_command), "%s", (char const*)b);
}
sint32 cu_esp_process_ipd(void){ return 0; }
void cu_esp_service_host_state(void){}
auint cu_esp_update_timer_counts(auint cycle){ (void)cycle; return test_state.read_buf_pos_in; }

static void init_uart(uint8 profile, uint32 route){
    memset(&test_state, 0, sizeof(test_state));
    memset(tcp_rx, 0, sizeof(tcp_rx)); memset(tcp_tx, 0, sizeof(tcp_tx));
    tcp_rx_pos = tcp_rx_len = tcp_tx_len = 0u;
    at_calls = 0u; at_command[0] = '\0';
    test_state.serial_route = route;
    test_state.emulation_model = 1u;
    test_state.uart_profile = profile;
    test_state.uart_baud_bits = 185u;
    test_state.uart_baud_bits_module = 185u;
    test_state.uart_data_bits = 8u;
    test_state.uart_stop_bits = 1u;
    test_state.uart_parity = 0u;
    test_state.uart_synchronous = 0u;
    test_state.uart_double_speed = 0u;
    test_state.uart_rx_enabled = 1u;
    test_state.uart_tx_enabled = 1u;
}

static int test_loopback_profile(uint8 profile){
    const auint frame = 29760u;
    auint st;

    init_uart(profile, CU_ESP_SERIAL_LOOPBACK);
    if(cu_uart_get_next_event_delay(0u) != frame) return 5 + profile;
    cu_esp_uzebox_write(0xA5u, 0u);
    if(cu_uart_get_next_event_delay(0u) != frame) return 6 + profile;
    st = cu_esp_uzebox_status(0u);
    if(!(st & (1u << UDRE0)) || (st & (1u << TXC0)) || (st & (1u << RXC0))) return 10 + profile;
    if(test_state.loopback_count != 0u) return 20 + profile;

    st = cu_esp_uzebox_status(frame - 1u);
    if(test_state.loopback_count != 0u || (st & (1u << RXC0))) return 30 + profile;

    st = cu_esp_uzebox_status(frame);
    if(!(st & (1u << TXC0)) || (st & (1u << RXC0))) return 40 + profile;
    if(test_state.loopback_count != 0u || !test_state.uart_rx_pending_valid) return 50 + profile;

    st = cu_esp_uzebox_status((frame * 2u) - 1u);
    if(st & (1u << RXC0)) return 60 + profile;
    st = cu_esp_uzebox_status(frame * 2u);
    if(!(st & (1u << RXC0))) return 70 + profile;
    if(cu_esp_uzebox_read(frame * 2u) != 0xA5u) return 80 + profile;

    init_uart(profile, CU_ESP_SERIAL_LOOPBACK);
    cu_esp_uzebox_write(0x11u, 0u);
    cu_esp_uzebox_write(0x22u, 1u);
    st = cu_esp_uzebox_status(1u);
    if(st & (1u << UDRE0)) return 100 + profile;
    st = cu_esp_uzebox_status(frame);
    if(!(st & (1u << UDRE0)) || (st & (1u << TXC0))) return 110 + profile;
    st = cu_esp_uzebox_status(frame * 2u);
    if(!(st & (1u << TXC0)) || !(st & (1u << RXC0))) return 120 + profile;
    if(cu_esp_uzebox_read(frame * 2u) != 0x11u) return 130 + profile;
    st = cu_esp_uzebox_status(frame * 3u);
    if(!(st & (1u << RXC0)) || cu_esp_uzebox_read(frame * 3u) != 0x22u) return 140 + profile;

    /* Leave the first receive UDR unread while a second frame completes. The
     * older byte must remain visible and DOR must assert. */
    init_uart(profile, CU_ESP_SERIAL_LOOPBACK);
    cu_esp_uzebox_write(0x31u, 0u);
    cu_esp_uzebox_write(0x32u, 1u);
    (void)cu_esp_uzebox_status(frame);
    (void)cu_esp_uzebox_status(frame * 2u);
    st = cu_esp_uzebox_status(frame * 3u);
    if(!(st & (1u << RXC0)) || !(st & (1u << DOR0))) return 150 + profile;
    if(cu_esp_uzebox_read(frame * 3u) != 0x31u) return 160 + profile;
    return 0;
}

static int test_tcp_endpoint(void){
    const auint frame = 29760u;
    init_uart(CU_UART_PROFILE_FAST, CU_ESP_SERIAL_TCP_SERIAL);

    cu_esp_uzebox_write(0x73u, 0u);
    if(tcp_tx_len != 0u) return 200;
    (void)cu_esp_uzebox_status(frame - 1u);
    if(tcp_tx_len != 0u) return 201;
    (void)cu_esp_uzebox_status(frame);
    if(tcp_tx_len != 1u || tcp_tx[0] != 0x73u) return 202;

    tcp_rx[0] = 0x4Cu; tcp_rx_len = 1u;
    (void)cu_esp_uzebox_status(frame);
    if(cu_esp_uzebox_status((frame * 2u) - 1u) & (1u << RXC0)) return 203;
    if(!(cu_esp_uzebox_status(frame * 2u) & (1u << RXC0))) return 204;
    if(cu_esp_uzebox_read(frame * 2u) != 0x4Cu) return 205;
    return 0;
}

static int test_esp_endpoint(void){
    const auint frame = 29760u;
    const uint8 cmd[4] = {'A','T','\r','\n'};
    unsigned i;

    init_uart(CU_UART_PROFILE_FAST, CU_ESP_SERIAL_ESP_MODULE);
    for(i = 0u; i < 4u; ++i){
        auint cycle = frame * i;
        (void)cu_esp_uzebox_status(cycle);
        cu_esp_uzebox_write(cmd[i], cycle);
    }
    if(at_calls != 0u) return 220;
    (void)cu_esp_uzebox_status(frame * 4u);
    if(at_calls != 1u || strcmp(at_command, "AT\r\n") != 0) return 221;

    test_state.read_buf[0] = 'O';
    test_state.read_buf[1] = 'K';
    test_state.read_buf_pos_out = 0u;
    test_state.read_buf_pos_in = 2u;
    (void)cu_esp_uzebox_status(frame * 4u);
    if(cu_esp_uzebox_status((frame * 5u) - 1u) & (1u << RXC0)) return 222;
    if(cu_esp_uzebox_read(frame * 5u) != 'O') return 223;
    if(cu_esp_uzebox_read(frame * 6u) != 'K') return 224;
    return 0;
}

int main(void){
    int rc;
    rc = test_loopback_profile(CU_UART_PROFILE_FAST); if(rc) return rc;
    rc = test_loopback_profile(CU_UART_PROFILE_BALANCED); if(rc) return rc;
    rc = test_loopback_profile(CU_UART_PROFILE_DEBUG); if(rc) return rc;
    rc = test_tcp_endpoint(); if(rc) return rc;
    rc = test_esp_endpoint(); if(rc) return rc;
    puts("unified UART core OK: ESP, TCP and loopback share cycle timing and buffering");
    return 0;
}
