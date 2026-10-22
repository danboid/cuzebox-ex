#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "esp_rom.h"
#define RESULT (*(volatile uint8_t*)0x800)
static volatile uint8_t rx[128],head,tail;
static void pump(void){while(UCSR0A&_BV(RXC0)){uint8_t b=UDR0;rx[head++&127]=b;}}
void EspUartPut(u8 b){while(!(UCSR0A&_BV(UDRE0))){}UDR0=b;}
s16 EspUartGet(void){pump();return tail==head?-1:rx[tail++&127];}
void EspUartFlushRx(void){tail=head;}
void WaitVsync(int count){while(count--)for(unsigned i=0;i<340;i++){pump();_delay_us(50);}}
static void reset(u8 loader){
 PORTD&=~8;DDRD|=8;
 PORTA&=~128;if(loader)DDRA|=128;else DDRA&=~128;
 _delay_ms(40);DDRD&=~8;_delay_ms(140);DDRA&=~128;_delay_ms(30);
 EspUartFlushRx();
}
static void check(u8 ok,u8 error){if(!ok){RESULT=error;for(;;){}}}
int main(void){
 RESULT=1;UCSR0A=0;UBRR0H=0;UBRR0L=15;UCSR0C=6;
 UCSR0B=_BV(RXEN0)|_BV(TXEN0);
 reset(1);RESULT=2;check(EspRomSync(115200),0xe1);
 u32 id;check(EspRomFlashId(&id)&&id==0x1640ef,0xe2);
 RESULT=3;check(EspFlashBegin(0,2048),0xe3);
 u8 block[1024];for(unsigned i=0;i<1024;i++)block[i]=(u8)i;
 check(EspFlashBlock(block,1024,0),0xe4);
 reset(0);RESULT=4;
 reset(1);check(EspRomSync(115200),0xe5);
 check(EspFlashBegin(0,2048),0xe6);
 check(EspFlashBlock(block,1024,0),0xe7);
 check(EspFlashBlock(block,1024,1),0xe8);
 RESULT=0x80;for(;;){}
}
