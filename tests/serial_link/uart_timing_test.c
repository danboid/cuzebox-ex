#include "cu_esp.h"
#include "cu_uart.h"
#include <stdio.h>
#include <string.h>

static cu_state_esp_t test_state;
cu_state_esp_t* cu_esp_get_state(void){ return &test_state; }

int main(void){
    memset(&test_state, 0, sizeof(test_state));
    test_state.uart_baud_bits = 185u; /* Catacombs: 28.63636 MHz / 16 / 9600 - 1 */
    test_state.uart_data_bits = 8u;
    test_state.uart_stop_bits = 1u;
    test_state.uart_parity = 0u;
    test_state.uart_synchronous = 0u;
    test_state.uart_double_speed = 0u;
    test_state.uart_profile = CU_UART_PROFILE_FAST;

    if(cu_uart_get_bit_cycles() != 2976u) return 20;
    if(cu_uart_get_frame_bits() != 10u) return 21;
    if(cu_uart_get_frame_cycles() != 29760u) return 22;

    test_state.uart_profile = CU_UART_PROFILE_BALANCED;
    if(cu_uart_get_frame_cycles() != 29760u) return 23;

    puts("uart 9600 8N1 timing OK: 2976 cycles/bit, 29760 cycles/frame");
    return 0;
}
