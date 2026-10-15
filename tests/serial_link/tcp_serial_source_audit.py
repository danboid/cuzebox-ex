#!/usr/bin/env python3
"""Static guardrails for ESP-preserving TCP Serial endpoint servicing."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
net = (root / "cu_esp_net.c").read_text(encoding="utf-8")
ui = (root / "microui" / "mui_integration.c").read_text(encoding="utf-8")
hdr = (root / "cu_esp.h").read_text(encoding="utf-8")
frame = (root / "frame.c").read_text(encoding="utf-8")
uart = (root / "cu_uart.c").read_text(encoding="utf-8")
avr = (root / "cu_avr.c").read_text(encoding="utf-8")

required_net = [
    'cu_tcp_diag_log_line("SNAP"',
    'cu_tcp_diag_log_line("SEND_ERROR"',
    'cu_tcp_diag_log_line("RECV_ERROR"',
    'cu_tcp_diag_log_line("SOCKET_LOSS"',
    'TCP_NODELAY',
    'void cu_esp_endpoint_host_tick(void)',
    'esp_state.serial_route == CU_ESP_SERIAL_TCP_SERIAL',
]
for item in required_net:
    assert item in net, f"missing network diagnostic/endpoint guardrail: {item}"

required_header = [
    'last_send_error;',
    'last_recv_error;',
    'socket_rx_pending_bytes;',
    'max_service_gap_ms;',
    'cu_esp_tcp_serial_diag_get(',
    'cu_esp_endpoint_host_tick(',
]
for item in required_header:
    assert item in hdr, f"missing public diagnostic field/API: {item}"

# Local presets must connect regardless of which CUzeBox instance is started
# first. The game protocol still rejects a fresh stream after a gameplay loss.
server = ui.split('static void mui_apply_tcp_serial_local_server(void)', 1)[1].split('static void mui_apply_tcp_serial_local_client(void)', 1)[0]
client = ui.split('static void mui_apply_tcp_serial_local_client(void)', 1)[1].split('static char const* mui_esp_wifi_mode_name', 1)[0]
assert 'mui_cfg_tcp_serial_auto_reconnect = 1;' in server
assert 'mui_cfg_tcp_serial_auto_reconnect = 1;' in client

# Host socket servicing is separate from AVR-visible UART timing.
assert 'cu_esp_endpoint_host_tick();' in frame
assert 'cu_uart_get_next_event_delay' not in uart
assert 'cu_uart_get_next_event_delay' not in avr


# UCSR0A/UDR polling must never issue host socket calls. The raw endpoint
# functions are pure queue operations; only the host tick services TCP.
def body(name: str, next_name: str) -> str:
    return net.split(name, 1)[1].split(next_name, 1)[0]

write_body = body('void cu_esp_tcp_serial_write(uint8 c)', 'uint8 cu_esp_tcp_serial_read(void)')
read_body = body('uint8 cu_esp_tcp_serial_read(void)', 'auint cu_esp_tcp_serial_rx_bytes_ready(void)')
ready_body = body('auint cu_esp_tcp_serial_rx_bytes_ready(void)', 'void cu_esp_endpoint_host_tick(void)')
diag_body = body('void cu_esp_tcp_serial_diag_get(cu_esp_tcp_serial_diag_t *out)', 'void cu_esp_tcp_serial_diag_reset(void)')
state_body = body('auint cu_esp_get_tcp_serial_state(void)', 'sint32 cu_esp_get_tcp_serial_last_error(void)')
error_body = body('sint32 cu_esp_get_tcp_serial_last_error(void)', 'char const* cu_esp_get_tcp_serial_host(void)')
for label, section in [('write', write_body), ('read', read_body), ('ready', ready_body),
                       ('diag', diag_body), ('state', state_body), ('error', error_body)]:
    assert 'cu_esp_tcp_serial_service();' not in section, f'{label} path must not service host sockets'
assert net.count('cu_esp_tcp_serial_service();') == 1, 'host tick must be the sole TCP service caller'
assert 'cu_esp_endpoint_host_tick();' in frame.split('auint frame_run(', 1)[1].split('while(1)', 1)[0]
assert frame.count('cu_esp_endpoint_host_tick();') == 1, 'host endpoint service must occur once at video-frame entry'

# Raw endpoints use the AVR UDR+shift-register path even when the UI profile is FAST,
# and must never leak bytes into the ESP AT parser.
assert 'if(cu_uart_route_is_passthrough() || cu_uart_route_uses_esp_rules())\n\t\treturn TRUE;' in uart
assert 'if(cu_uart_route_is_passthrough()){' in uart
assert 'cu_uart_backend_write(send_val, cycle);' in uart
assert 'cu_uart_tx_emit_at_frame_end' in uart

# Keep polling-based AVR integration and the TX interrupt gate. The ESP route
# may improve its internal UDR/shift timing, but must not install replacement
# RX/UDRE interrupt machinery or move host TCP work into register polling.
assert 'cu_uart_poll_cache_refresh_esp' in uart
assert 'cu_esp_update_timer_counts(cycle)' in uart
assert 'cu_esp_process_ipd()' in uart
assert 'VECT_USART0_RX' not in avr
assert 'VECT_USART0_UDRE' not in avr
assert 'TXCIE0' in avr

assert '760, 560' in ui, 'Serial diagnostics window should remain wide/tall enough for live counters'
assert 'LAST SOCKET ERROR: SEND %d  RECV %d' in ui

print('poll-buffered UART / passive TCP endpoint source audit OK')
