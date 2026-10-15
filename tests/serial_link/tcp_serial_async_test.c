#include "cu_esp.h"
#include "cu_uart.h"
#include "cu_avr.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

static cu_state_esp_t test_state;
static unsigned at_parse_calls;
cu_state_esp_t* cu_esp_get_state(void){ return &test_state; }

/* cu_uart.c keeps the established ESP path available, but this test exercises
 * only the raw TCP endpoint. Stub the ESP-command and MIDI dependencies. */
void cu_esp_serial_midi_write(uint8 c){ (void)c; }
uint8 cu_esp_serial_midi_read(void){ return 0u; }
auint cu_esp_serial_midi_rx_bytes_ready(void){ return 0u; }
void cu_esp_txp(const char *s){ (void)s; }
void cu_esp_txl(const char *s, auint l){ (void)s; (void)l; }
void cu_esp_txi(sint32 v){ (void)v; }
void cu_esp_txp_error(void){}
void cu_esp_txp_ok(void){}
void cu_esp_process_at(sint8 *b){ (void)b; at_parse_calls++; }
auint cu_esp_update_timer_counts(auint cycle){ return cycle; }
sint32 cu_esp_process_ipd(void){ return 0; }
void cu_esp_service_host_state(void){}
uint8 cu_esp_lan_virtual_link_open(cu_state_esp_t *es, uint32 sock){ (void)es; (void)sock; return 0u; }
sint32 cu_esp_lan_virtual_send(cu_state_esp_t *es, uint32 sock, const uint8 *buf, uint16 len){ (void)es; (void)sock; (void)buf; (void)len; return -1; }

struct echo_ctx {
    _Atomic unsigned port;
    _Atomic int ready;
    _Atomic int ok;
};

static void* echo_server(void* opaque){
    struct echo_ctx* ctx = (struct echo_ctx*)opaque;
    int ls = -1, cs = -1;
    struct sockaddr_in sa;
    socklen_t salen = sizeof(sa);
    unsigned char b = 0;
    int one = 1;

    ls = socket(AF_INET, SOCK_STREAM, 0);
    if(ls < 0) return NULL;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = 0;
    if(bind(ls, (struct sockaddr*)&sa, sizeof(sa)) != 0) goto out;
    if(getsockname(ls, (struct sockaddr*)&sa, &salen) != 0) goto out;
    if(listen(ls, 1) != 0) goto out;
    atomic_store(&ctx->port, (unsigned)ntohs(sa.sin_port));
    atomic_store(&ctx->ready, 1);

    cs = accept(ls, NULL, NULL);
    if(cs < 0) goto out;
    if(recv(cs, &b, 1, 0) != 1) goto out;
    if(b != 0xA7) goto out;
    b = 0x5C;
    if(send(cs, &b, 1, 0) != 1) goto out;
    atomic_store(&ctx->ok, 1);
out:
    if(cs >= 0) close(cs);
    if(ls >= 0) close(ls);
    return NULL;
}


struct raw_client_ctx {
    unsigned port;
    _Atomic int ok;
};

static unsigned reserve_loopback_port(void){
    int s;
    struct sockaddr_in sa;
    socklen_t salen = sizeof(sa);
    unsigned port = 0u;
    s = socket(AF_INET, SOCK_STREAM, 0);
    if(s < 0) return 0u;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = 0;
    if(bind(s, (struct sockaddr*)&sa, sizeof(sa)) == 0 &&
       getsockname(s, (struct sockaddr*)&sa, &salen) == 0)
        port = (unsigned)ntohs(sa.sin_port);
    close(s);
    return port;
}

static void* raw_client(void* opaque){
    struct raw_client_ctx* ctx = (struct raw_client_ctx*)opaque;
    struct sockaddr_in sa;
    unsigned char b;
    unsigned i;
    int s = -1;

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = htons((uint16_t)ctx->port);
    for(i = 0; i < 2000; ++i){
        s = socket(AF_INET, SOCK_STREAM, 0);
        if(s >= 0 && connect(s, (struct sockaddr*)&sa, sizeof(sa)) == 0) break;
        if(s >= 0) close(s);
        s = -1;
        usleep(1000);
    }
    if(s < 0) return NULL;
    b = 0x3A;
    if(send(s, &b, 1, 0) != 1) goto out;
    if(recv(s, &b, 1, 0) != 1) goto out;
    if(b != 0xC7) goto out;
    atomic_store(&ctx->ok, 1);
out:
    close(s);
    return NULL;
}

int main(void){
    pthread_t th;
    struct echo_ctx ctx;
    unsigned i;
    unsigned port;
    cu_esp_tcp_serial_diag_t diag;
    char log_path[320];

    memset(&test_state, 0, sizeof(test_state));
    test_state.serial_route = CU_ESP_SERIAL_TCP_SERIAL;
    test_state.emulation_model = 1u; /* Raw bytes must still bypass ESP AT. */
    test_state.user_input_mode = ESP_USER_MODE_AT;
    test_state.uart_profile = CU_UART_PROFILE_FAST;
    test_state.uart_tx_enabled = 1u;
    test_state.uart_rx_enabled = 1u;
    test_state.uart_baud_bits = 185u;
    test_state.uart_data_bits = 8u;
    test_state.uart_stop_bits = 1u;
    atomic_init(&ctx.port, 0u);
    atomic_init(&ctx.ready, 0);
    atomic_init(&ctx.ok, 0);

    if(pthread_create(&th, NULL, echo_server, &ctx) != 0) return 10;
    for(i = 0; i < 1000 && !atomic_load(&ctx.ready); ++i) usleep(1000);
    if(!atomic_load(&ctx.ready)) return 11;
    port = atomic_load(&ctx.port);
    if(port == 0u) return 12;

    cu_esp_set_tcp_serial_host("127.0.0.1");
    cu_esp_set_tcp_serial_port(port);
    cu_esp_set_tcp_serial_auto_reconnect(FALSE);
    cu_esp_set_tcp_serial_mode(CU_ESP_TCP_SERIAL_MODE_CLIENT);
    if(cu_esp_tcp_serial_start() != 0) return 13;

    for(i = 0; i < 5000; ++i){
        cu_esp_endpoint_host_tick();
        if(cu_esp_get_tcp_serial_state() == CU_ESP_TCP_SERIAL_STATE_CONNECTED)
            break;
        usleep(1000);
    }
    if(cu_esp_get_tcp_serial_state() != CU_ESP_TCP_SERIAL_STATE_CONNECTED) return 14;

    /* AVR-facing endpoint accessors and UI diagnostics must be passive. */
    cu_esp_tcp_serial_diag_get(&diag);
    {
        uint32 service_calls = diag.service_calls;
        (void)cu_esp_get_tcp_serial_state();
        (void)cu_esp_get_tcp_serial_last_error();
        (void)cu_esp_tcp_serial_rx_bytes_ready();
        cu_esp_tcp_serial_diag_get(&diag);
        if(diag.service_calls != service_calls) return 40;
        cu_esp_uzebox_write(0xA7u, 0u);
        cu_esp_tcp_serial_diag_get(&diag);
        if(diag.service_calls != service_calls) return 41;
        if(at_parse_calls != 0u || test_state.write_buf_pos_in != 0u) return 43;
    }
    for(i = 0; i < 5000; ++i){
        cu_esp_endpoint_host_tick();
        if(cu_esp_tcp_serial_rx_bytes_ready())
            break;
        usleep(1000);
    }
    if(!cu_esp_tcp_serial_rx_bytes_ready()) return 15;
    cu_esp_tcp_serial_diag_get(&diag);
    {
        uint32 service_calls = diag.service_calls;
        if((cu_esp_uzebox_status(100000u) & (1u << RXC0)) == 0u) return 44;
        if(cu_esp_uzebox_read(100000u) != 0x5C) return 16;
        cu_esp_tcp_serial_diag_get(&diag);
        if(diag.service_calls != service_calls) return 42;
    }

    /* A deliberate host-side pause must be visible as an exact service gap. */
    usleep(60000);
    cu_esp_endpoint_host_tick();
    cu_esp_tcp_serial_diag_get(&diag);
    if(diag.max_service_gap_ms < 50u || diag.service_gap_over_50ms == 0u) return 26;

    pthread_join(th, NULL);
    for(i = 0; i < 100 && cu_esp_get_tcp_serial_state() == CU_ESP_TCP_SERIAL_STATE_CONNECTED; ++i){
        cu_esp_endpoint_host_tick();
        usleep(1000);
    }
    cu_esp_tcp_serial_diag_get(&diag);
    if(diag.avr_to_backend_bytes != 1u || diag.backend_to_avr_bytes != 1u) return 18;
    if(diag.socket_tx_bytes != 1u || diag.socket_rx_bytes != 1u) return 19;
    if(diag.connect_count != 1u) return 20;
    if(cu_esp_get_tcp_serial_state() == CU_ESP_TCP_SERIAL_STATE_CONNECTING) return 21;
    snprintf(log_path, sizeof(log_path), "%s", cu_esp_tcp_serial_diag_get_log_path());
    if(log_path[0] == '\0') return 22;
    cu_esp_tcp_serial_diag_flush_log();
    cu_esp_tcp_serial_end();
    {
        FILE *f = fopen(log_path, "rb");
        char text[8192];
        size_t n;
        if(f == NULL) return 23;
        n = fread(text, 1, sizeof(text) - 1u, f);
        fclose(f);
        text[n] = '\0';
        if(strstr(text, "client_connected") == NULL) return 24;
        if(strstr(text, "tcp_serial_end") == NULL) return 25;
        if(strstr(text, "SERVICE_GAP") == NULL) return 27;
        remove(log_path);
    }
    if(!atomic_load(&ctx.ok)) return 17;

    /* Server role must also honor AUTO RECONNECT=off after its peer closes. */
    {
        struct raw_client_ctx sctx;
        pthread_t cth;
        unsigned sport = reserve_loopback_port();
        if(sport == 0u) return 30;
        sctx.port = sport;
        atomic_init(&sctx.ok, 0);
        cu_esp_set_tcp_serial_port(sport);
        cu_esp_set_tcp_serial_auto_reconnect(FALSE);
        cu_esp_set_tcp_serial_mode(CU_ESP_TCP_SERIAL_MODE_SERVER);
        if(cu_esp_tcp_serial_start() != 0) return 31;
        if(pthread_create(&cth, NULL, raw_client, &sctx) != 0) return 32;
        for(i = 0; i < 5000 && !cu_esp_tcp_serial_rx_bytes_ready(); ++i){
            cu_esp_endpoint_host_tick();
            usleep(1000);
        }
        if(!cu_esp_tcp_serial_rx_bytes_ready()) return 33;
        if(cu_esp_tcp_serial_read() != 0x3A) return 34;
        cu_esp_tcp_serial_write(0xC7);
        for(i = 0; i < 1000 && !atomic_load(&sctx.ok); ++i){
            cu_esp_endpoint_host_tick();
            usleep(1000);
        }
        pthread_join(cth, NULL);
        if(!atomic_load(&sctx.ok)) return 35;
        for(i = 0; i < 5000; ++i){
            cu_esp_endpoint_host_tick();
            if(cu_esp_get_tcp_serial_state() == CU_ESP_TCP_SERIAL_STATE_DISCONNECTED) break;
            usleep(1000);
        }
        if(cu_esp_get_tcp_serial_state() != CU_ESP_TCP_SERIAL_STATE_DISCONNECTED) return 36;
        if(cu_esp_get_tcp_serial_state() == CU_ESP_TCP_SERIAL_STATE_LISTENING) return 37;
        snprintf(log_path, sizeof(log_path), "%s", cu_esp_tcp_serial_diag_get_log_path());
        cu_esp_tcp_serial_end();
        if(log_path[0] != '\0') remove(log_path);
    }

    puts("tcp serial client/server echo/diagnostics/reconnect policy OK");
    return 0;
}
