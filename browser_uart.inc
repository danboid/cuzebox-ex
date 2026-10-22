/* Browser UART adapter: native builds and all other peripherals stay unchanged. */
#include <emscripten.h>
#include "cu_avr.h"
#include <string.h>
static unsigned web_phase,web_low,web_reset_low;
static auint web_edge;
static void web_gpio_tick(auint cycle);
static unsigned web_rx_head,web_rx_tail;
static uint8 web_rx[4096];
static auint web_rx_at,web_tx_at;
static unsigned web_rx_wait,web_connected;
static auint web_service_cycle;
static char web_at_line[64];
static unsigned web_at_size,web_at_overflow;
static auint web_uart_byte_cycles(void){
    const uint8 *io=cu_avr_get_state()->iors;
    unsigned divider=io[0xc4]|((io[0xc5]&15U)<<8);
    return (divider+1U)*((io[0xc0]&2U)?8U:16U)*10U;
}
extern void web_endpoint_connect(unsigned);
extern void web_endpoint_byte(uint8_t);
extern void web_net_tick(void);
EM_JS(void, web_send_byte, (unsigned value), {
    if(Module.uzenetByte) Module.uzenetByte(value);
});
EM_JS(void, web_reset_connection, (), {
    if(Module.uzenetReset) Module.uzenetReset();
});
EMSCRIPTEN_KEEPALIVE void web_uart_connect(unsigned enabled){
    web_connected=enabled;web_phase=web_low=0;web_rx_head=web_rx_tail=web_rx_wait=0;
    web_at_size=web_at_overflow=0;
    web_service_cycle=cu_avr_get_state()->cycle;
    web_endpoint_connect(enabled);
}
EMSCRIPTEN_KEEPALIVE int web_uart_push(unsigned value){
    unsigned next=(web_rx_head+1)&4095;
    if(next==web_rx_tail)return 0;
    web_rx[web_rx_head]=(uint8)value;web_rx_head=next;return 1;
}
static void web_at_reply(const char *text){
    while(*text)web_uart_push((uint8)*text++);
}
/* Provisioned browser connections expose only the UART bootstrap commands.
 * Wi-Fi configuration and flashing remain native-emulator features. */
static void web_at_byte(uint8 value){
    if(value=='\n'){
        web_at_line[web_at_size]=0;
        if(!web_at_overflow && (!strcmp(web_at_line,"AT") ||
           !strcmp(web_at_line,"AT+UART_CUR=57600,8,1,0,0") ||
           !strcmp(web_at_line,"AT+UART_CUR=115200,8,1,0,0") ||
           !strcmp(web_at_line,"AT+UART_CUR=9600,8,1,0,0")))web_at_reply("\r\nOK\r\n");
        else if(!web_at_overflow && !strcmp(web_at_line,"AT+CIPSEND"))web_at_reply("\r\nOK\r\n>");
        else web_at_reply("\r\nERROR\r\n");
        web_at_size=web_at_overflow=0;
    }else if(value!='\r'){
        if(web_at_size<sizeof(web_at_line)-1)web_at_line[web_at_size++]=(char)value;
        else web_at_overflow=1;
        if(web_at_size==3 && !memcmp(web_at_line,"+++",3)){
            web_at_size=0;web_at_reply("\r\nOK\r\n");
        }
    }
}
void cu_esp_uzebox_write(uint8 val,auint cycle){
    web_tx_at=WRAP32(cycle+web_uart_byte_cycles());
    if(web_connected){web_send_byte(val);if(web_phase==3)web_endpoint_byte(val);else web_at_byte(val);}
}
auint cu_esp_uzebox_status(auint cycle){
    web_gpio_tick(cycle);
    auint flags=0;
    if((sint32)(cycle-web_tx_at)>=0)flags=(1u<<UDRE0)|(1u<<TXC0);
    if(web_rx_head!=web_rx_tail && (web_phase<2 || (web_phase==3 && web_low))){
        if(!web_rx_wait){web_rx_at=WRAP32(cycle+web_uart_byte_cycles());web_rx_wait=1;}
        if((sint32)(cycle-web_rx_at)>=0)flags|=(1u<<RXC0);
    }
    return flags;
}
auint cu_esp_uzebox_read(auint cycle){
    if(!(cu_esp_uzebox_status(cycle)&(1u<<RXC0)))return 255;
    auint value=web_rx[web_rx_tail];web_rx_tail=(web_rx_tail+1)&4095;
    web_rx_wait=0;return value;
}
void cu_esp_uzebox_modify(auint port,auint val,auint cycle){(void)port;(void)val;(void)cycle;}
auint cu_esp_uzebox_read_ready(auint cycle){return cu_esp_uzebox_status(cycle)&(1u<<RXC0);}
auint cu_esp_uzebox_write_ready(auint cycle){return cu_esp_uzebox_status(cycle)&((1u<<UDRE0)|(1u<<TXC0));}
void cu_esp_reset_uart(void){web_rx_head=web_rx_tail=web_rx_wait=0;web_tx_at=0;}

/* GPIO0/PA7 open-drain entry and flow control, in emulated time. */
static void web_gpio_tick(auint cycle){
    if(!web_connected)return;
    if((uint32)(cycle-web_service_cycle)>=28636U){
        web_service_cycle=cycle;web_net_tick();
    }
    if(web_phase==2 && (uint32)(cycle-web_edge)>=1145454U){
        static const uint8 body[]={0x80,85,78,4,2,255,1,0,13,12,40};
        uint8 crc=0;unsigned i,j;
        web_phase=3;
        web_uart_push(126);
        for(i=0;i<=sizeof(body);i++){
            uint8 b=i<sizeof(body)?body[i]:crc;
            if(i<sizeof(body)){crc^=b;for(j=0;j<8;j++)crc=(uint8)((crc<<1)^((crc&128)?7:0));}
            if(b==125 || b==126){web_uart_push(125);b^=32;}
            web_uart_push(b);
        }
        web_uart_push(126);
    }
}
void web_uart_gpio(auint ddra,auint porta,auint cycle){
    unsigned low=(ddra&128) && !(porta&128);
    web_gpio_tick(cycle);
    if(web_connected && low!=web_low){
        if(web_phase==0 && low){web_phase=1;web_edge=cycle;}
        else if(web_phase==1 && !low){
            auint duration=WRAP32(cycle-web_edge);
            web_phase=(duration>=859090U && duration<=2577273U)?2:0;
            web_edge=cycle;
        }
    }
    web_low=low;
}
auint web_uart_pina(auint value,auint cycle){
    web_gpio_tick(cycle);
    if(!web_connected)return value;
    return (value&127)|((web_low || web_phase==2)?0:128);
}
void web_uart_reset_gpio(auint ddrd,auint portd){
    unsigned low=(ddrd&8) && !(portd&8);
    if(low && !web_reset_low && web_connected){
        web_uart_connect(0);
        web_reset_connection();
    }
    web_reset_low=low;
}

/* Read-only local diagnostics for repeatable browser cartridge tests. */
#include "cu_ctr.h"
EMSCRIPTEN_KEEPALIVE unsigned web_uart_diagnostic(void){
    auint buttons=0;cu_ctr_getsnes_state(0,&buttons);
    return web_phase|(web_low<<4)|(buttons<<8);
}
EMSCRIPTEN_KEEPALIVE unsigned web_read_sram(unsigned address){
    return cu_avr_get_state()->sram[address&4095];
}
