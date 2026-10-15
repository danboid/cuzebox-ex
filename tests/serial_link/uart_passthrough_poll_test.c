#include "cu_esp.h"
#include "cu_uart.h"
#include "cu_avr.h"
#include <stdio.h>
#include <string.h>

static cu_state_esp_t test_state;
static uint8 tx_bytes[64];
static unsigned tx_count;
static uint8 rx_bytes[64];
static unsigned rx_head, rx_tail, rx_count;
static unsigned at_calls;

cu_state_esp_t* cu_esp_get_state(void){ return &test_state; }

sint32 cu_esp_host_serial_start(void){ return -1; }
void cu_esp_host_serial_end(void){}
void cu_esp_host_serial_write(uint8 c){ (void)c; }
uint8 cu_esp_host_serial_read(void){ return 0; }
auint cu_esp_host_serial_rx_bytes_ready(void){ return 0; }

sint32 cu_esp_tcp_serial_start(void){ return 0; }
void cu_esp_tcp_serial_end(void){}
void cu_esp_tcp_serial_write(uint8 c){ if(tx_count < sizeof(tx_bytes)) tx_bytes[tx_count++] = c; }
uint8 cu_esp_tcp_serial_read(void){
    uint8 c = 0;
    if(rx_count){ c = rx_bytes[rx_head++ % sizeof(rx_bytes)]; rx_count--; }
    return c;
}
auint cu_esp_tcp_serial_rx_bytes_ready(void){ return rx_count; }

void cu_esp_serial_midi_write(uint8 c){ (void)c; }
uint8 cu_esp_serial_midi_read(void){ return 0; }
auint cu_esp_serial_midi_rx_bytes_ready(void){ return 0; }

void cu_esp_txp(const char *s){ (void)s; }
void cu_esp_txl(const char *s, auint l){ (void)s; (void)l; }
void cu_esp_txi(sint32 v){ (void)v; }
void cu_esp_txp_error(void){}
void cu_esp_txp_ok(void){}
sint32 cu_esp_mqtt_publish(const char *topic, const uint8 *data, uint32 len, uint8 qos, uint8 retain){(void)topic;(void)data;(void)len;(void)qos;(void)retain;return -1;}
sint32 cu_esp_net_send(uint32 s, sint8 *b, sint32 l, sint32 f){ (void)s; (void)b; (void)l; (void)f; return -1; }
void cu_esp_process_at(sint8 *b){ (void)b; at_calls++; }
auint cu_esp_update_timer_counts(auint cycle){ return cycle; }
sint32 cu_esp_process_ipd(void){ return 0; }
void cu_esp_service_host_state(void){}

static void push_rx(uint8 c){ rx_bytes[rx_tail++ % sizeof(rx_bytes)] = c; rx_count++; }
static int require(int cond, int code, const char *message){
    if(!cond){ fprintf(stderr, "FAIL %d: %s\n", code, message); return code; }
    return 0;
}

int main(void){
    auint flags;
    auint cycle;
    unsigned i;
    int rc;
    static const uint8 incoming[] = {0xA7,0x22,0x01,0x10,0x20,0x00,0x80,0x34};

    memset(&test_state, 0, sizeof(test_state));
    test_state.serial_route = CU_ESP_SERIAL_TCP_SERIAL;
    test_state.emulation_model = 1u; /* Deliberately enabled: raw bytes must bypass AT. */
    test_state.user_input_mode = ESP_USER_MODE_AT;
    test_state.uart_profile = CU_UART_PROFILE_FAST; /* Normal/default UI setting. */
    test_state.uart_tx_enabled = 1u;
    test_state.uart_rx_enabled = 1u;
    test_state.uart_baud_bits = 185u;
    test_state.uart_data_bits = 8u;
    test_state.uart_stop_bits = 1u;

    flags = cu_esp_uzebox_status(0u);
    if((rc=require((flags & (1u<<UDRE0)) != 0u,10,"idle UDRE missing"))) return rc;
    if((rc=require((flags & (1u<<TXC0)) != 0u,11,"idle TXC missing"))) return rc;

    cu_esp_uzebox_write(0xA7u, 0u);
    if((rc=require(tx_count == 1u && tx_bytes[0] == 0xA7u,12,"first byte did not launch"))) return rc;
    if((rc=require(at_calls == 0u && test_state.write_buf_pos_in == 0u,13,"raw byte leaked into ESP AT parser"))) return rc;

    flags = cu_esp_uzebox_status(1u);
    if((rc=require((flags & (1u<<UDRE0)) != 0u,14,"UDRE did not reopen for UDR buffer"))) return rc;
    if((rc=require((flags & (1u<<TXC0)) == 0u,15,"TXC asserted while shift active"))) return rc;

    cu_esp_uzebox_write(0x01u, 1u);
    if((rc=require(tx_count == 1u,16,"second byte launched before first frame completed"))) return rc;
    flags = cu_esp_uzebox_status(2u);
    if((rc=require((flags & (1u<<UDRE0)) == 0u,17,"UDRE asserted with UDR occupied"))) return rc;

    flags = cu_esp_uzebox_status(29760u);
    if((rc=require(tx_count == 2u && tx_bytes[1] == 0x01u,18,"buffered byte did not launch at frame boundary"))) return rc;
    if((rc=require((flags & (1u<<UDRE0)) != 0u,19,"UDRE missing after buffered launch"))) return rc;
    if((rc=require((flags & (1u<<TXC0)) == 0u,20,"TXC asserted before second frame completed"))) return rc;

    flags = cu_esp_uzebox_status(59520u);
    if((rc=require((flags & (1u<<TXC0)) != 0u,21,"TXC missing after final frame"))) return rc;

    for(i=0;i<sizeof(incoming);i++) push_rx(incoming[i]);
    cycle = 100000u;
    for(i=0;i<sizeof(incoming);i++){
        unsigned guard = 0;
        while((cu_esp_uzebox_status(cycle) & (1u<<RXC0)) == 0u){
            cycle += 1820u; /* Approximate HSYNC polling cadence. */
            if(++guard > 32u) return require(0,30,"RXC did not become ready");
        }
        if((rc=require((uint8)cu_esp_uzebox_read(cycle) == incoming[i],31,"RX byte order/cadence mismatch"))) return rc;
        cycle += 1820u;
    }
    if((rc=require(rx_count == 0u,32,"endpoint RX queue not drained"))) return rc;

    printf("poll-buffered raw UART OK: FAST profile preserves UDR+shift buffering; 8 RX bytes ordered\n");
    return 0;
}
